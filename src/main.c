#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/time.h>
#include "token.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "diag.h"
#include "mxyconf.h"
#include "fmt.h"
#include "lint.h"
#include "flags.h"
#include "mxystdlib.h"

/* goose library headers */
#include "headers/main.h"
#include "headers/color.h"
#include "headers/config.h"
#include "headers/build.h"
#include "headers/pkg.h"
#include "headers/fs.h"
#include "headers/lock.h"

#define MOXY_CONFIG "moxy.yaml"
#define MOXY_LOCK   "moxy.lock"

/* ── helpers ─────────────────────────────────────────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "moxy: cannot open '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static char *try_read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static void dir_of(const char *path, char *buf, int bufsz) {
    const char *last = strrchr(path, '/');
    if (!last) {
        buf[0] = '.';
        buf[1] = '\0';
    } else {
        int len = (int)(last - path);
        if (len >= bufsz) len = bufsz - 1;
        memcpy(buf, path, len);
        buf[len] = '\0';
    }
}

static int ends_with(const char *s, const char *suffix) {
    int slen = (int)strlen(s);
    int suflen = (int)strlen(suffix);
    if (suflen > slen) return 0;
    return strcmp(s + slen - suflen, suffix) == 0;
}

static int is_project_mode(void) {
    return fs_exists(MOXY_CONFIG);
}

/* walk up from dir looking for moxy.yaml */
static char *find_project_yaml(const char *file_dir) {
    char dir[1024];
    char path[1024];
    if (file_dir) {
        strncpy(dir, file_dir, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        while (dir[0]) {
            snprintf(path, sizeof(path), "%s/%s", dir, MOXY_CONFIG);
            if (fs_exists(path)) return strdup(path);
            char *slash = strrchr(dir, '/');
            if (!slash) break;
            *slash = '\0';
        }
    }
    if (fs_exists(MOXY_CONFIG)) return strdup(MOXY_CONFIG);
    return NULL;
}

/* ── preprocessor ────────────────────────────────────────────── */

static char *preprocess(const char *src, const char *srcpath) {
    char basedir[512];
    dir_of(srcpath, basedir, sizeof(basedir));

    int cap = (int)strlen(src) * 2 + 4096;
    char *out = malloc(cap);
    int pos = 0;

    const char *p = src;
    while (*p) {
        const char *eol = strchr(p, '\n');
        int linelen = eol ? (int)(eol - p) : (int)strlen(p);

        const char *lp = p;
        while (*lp == ' ' || *lp == '\t') lp++;

        if (*lp == '@' && strncmp(lp, "@type", 5) == 0) {
            const char *cur = lp + 5;
            while (cur < p + linelen) {
                while (cur < p + linelen && (*cur == ' ' || *cur == '\t' || *cur == ',')) cur++;
                if (cur >= p + linelen || *cur == ';') break;
                const char *start = cur;
                while (cur < p + linelen && *cur != ',' && *cur != ';' && *cur != ' ' && *cur != '\t') cur++;
                if (cur > start) {
                    char tname[64];
                    int tlen = (int)(cur - start);
                    if (tlen > 63) tlen = 63;
                    memcpy(tname, start, tlen);
                    tname[tlen] = '\0';
                    parser_register_type(tname);
                }
            }
            p += linelen;
            if (eol) p++;
            continue;
        }

        if (*lp == '#' && strncmp(lp, "#include", 8) != 0) {
            char directive[512];
            int dlen = linelen < 511 ? linelen : 511;
            memcpy(directive, p, dlen);
            directive[dlen] = '\0';
            codegen_add_directive(directive);

            p += linelen;
            if (eol) p++;
            continue;
        }

        if (strncmp(lp, "#include", 8) == 0 && (lp[8] == ' ' || lp[8] == '\t' || lp[8] == '"' || lp[8] == '<')) {
            const char *after = lp + 8;
            while (*after == ' ' || *after == '\t') after++;

            char filename[256];
            int is_angle = 0;

            if (*after == '"') {
                after++;
                const char *end = strchr(after, '"');
                if (end) {
                    int flen = (int)(end - after);
                    memcpy(filename, after, flen);
                    filename[flen] = '\0';
                } else {
                    filename[0] = '\0';
                }
            } else if (*after == '<') {
                is_angle = 1;
                after++;
                const char *end = strchr(after, '>');
                if (end) {
                    int flen = (int)(end - after);
                    memcpy(filename, after, flen);
                    filename[flen] = '\0';
                } else {
                    filename[0] = '\0';
                }
            } else {
                filename[0] = '\0';
            }

            if (filename[0] && ends_with(filename, ".mxy")) {
                char fullpath[768];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, filename);

                char *inc_src = try_read_file(fullpath);
                const char *vpath = fullpath;

                if (!inc_src) {
                    const char *embedded = stdlib_lookup(filename);
                    if (embedded) {
                        inc_src = strdup(embedded);
                        vpath = filename;
                    }
                }

                if (!inc_src) {
                    fprintf(stderr, "moxy: cannot find '%s' (checked disk and stdlib)\n", filename);
                    exit(1);
                }

                char *processed = preprocess(inc_src, vpath);

                int plen = (int)strlen(processed);
                while (pos + plen + 2 >= cap) {
                    cap *= 2;
                    out = realloc(out, cap);
                }
                memcpy(out + pos, processed, plen);
                pos += plen;
                if (plen > 0 && processed[plen - 1] != '\n') {
                    out[pos++] = '\n';
                }

                free(processed);
                free(inc_src);
            } else if (filename[0]) {
                char directive[300];
                if (is_angle)
                    snprintf(directive, sizeof(directive), "#include <%s>", filename);
                else
                    snprintf(directive, sizeof(directive), "#include \"%s\"", filename);
                codegen_add_include(directive);
            }
        } else {
            while (pos + linelen + 2 >= cap) {
                cap *= 2;
                out = realloc(out, cap);
            }
            memcpy(out + pos, p, linelen);
            pos += linelen;
            if (eol) out[pos++] = '\n';
        }

        p += linelen;
        if (eol) p++;
    }

    out[pos] = '\0';
    return out;
}

/* ── transpile pipeline ──────────────────────────────────────── */

static const char *transpile(const char *path) {
    codegen_reset_includes();
    char *raw = read_file(path);
    char *src = preprocess(raw, path);
    free(raw);

    diag_init(src, path);

    Lexer lexer;
    lexer_init(&lexer, src);

    Token tokens[16384];
    int ntokens = 0;
    for (;;) {
        tokens[ntokens] = lexer_next(&lexer);
        if (tokens[ntokens].kind == TOK_EOF) { ntokens++; break; }
        ntokens++;
    }

    Node *program = parse(tokens, ntokens);
    const char *c_code = codegen(program);

    free(src);
    return c_code;
}

/* transpile a .mxy file to a .c file on disk */
static int transpile_to_file(const char *mxy_path, const char *c_path) {
    const char *c_code = transpile(mxy_path);
    FILE *f = fopen(c_path, "w");
    if (!f) {
        fprintf(stderr, "moxy: cannot write '%s'\n", c_path);
        return -1;
    }
    fputs(c_code, f);
    fclose(f);
    return 0;
}

/* ── single-file compile (for run/build on individual .mxy) ── */

static int compile_single(const char *cpath, const char *binpath, const char *srcdir) {
    const char *cc = getenv("CC");
    if (!cc) cc = "cc";

    const char *env_cflags = getenv("CFLAGS");
    const char *pthread_flag = moxy_async_enabled ? " -lpthread" : "";

    /* look for moxy.yaml in source directory for build flags */
    char proj_cflags[512] = {0};
    char proj_ldflags[512] = {0};
    char *ypath = find_project_yaml(srcdir);
    if (ypath) {
        Config cfg;
        if (config_load(ypath, &cfg) == 0) {
            if (cfg.cflags[0]) strncpy(proj_cflags, cfg.cflags, sizeof(proj_cflags) - 1);
            if (cfg.ldflags[0]) strncpy(proj_ldflags, cfg.ldflags, sizeof(proj_ldflags) - 1);
        }
        free(ypath);
    }

    char cmd[4096];
    int off = snprintf(cmd, sizeof(cmd), "%s -std=c11", cc);

    if (env_cflags) off += snprintf(cmd + off, sizeof(cmd) - off, " %s", env_cflags);
    if (proj_cflags[0]) off += snprintf(cmd + off, sizeof(cmd) - off, " %s", proj_cflags);

    off += snprintf(cmd + off, sizeof(cmd) - off, " -o '%s' '%s'", binpath, cpath);

    if (proj_ldflags[0]) off += snprintf(cmd + off, sizeof(cmd) - off, " %s", proj_ldflags);
    if (pthread_flag[0]) off += snprintf(cmd + off, sizeof(cmd) - off, "%s", pthread_flag);

    int status = system(cmd);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    return 0;
}

/* ── project-mode transpile all .mxy to build/gen/ ───────── */

static void collect_mxy_files(const char *dir, char files[][512], int *count, int max) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *count < max) {
        if (ent->d_name[0] == '.') continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        if (ent->d_type == DT_DIR) {
            collect_mxy_files(path, files, count, max);
        } else if (ends_with(ent->d_name, ".mxy")) {
            strncpy(files[*count], path, 511);
            files[*count][511] = '\0';
            (*count)++;
        }
    }
    closedir(d);
}

static int transpile_project_to(const Config *cfg, const char *gen_dir) {
    fs_mkdir(GOOSE_BUILD);
    fs_mkdir(gen_dir);

    char mxy_files[256][512];
    int mxy_count = 0;
    collect_mxy_files(cfg->src_dir, mxy_files, &mxy_count, 256);

    if (mxy_count == 0) return 0;

    for (int i = 0; i < mxy_count; i++) {
        const char *base = strrchr(mxy_files[i], '/');
        base = base ? base + 1 : mxy_files[i];

        char stem[256];
        strncpy(stem, base, sizeof(stem) - 1);
        stem[sizeof(stem) - 1] = '\0';
        char *dot = strrchr(stem, '.');
        if (dot) *dot = '\0';

        char out_path[512];
        snprintf(out_path, sizeof(out_path), "%s/%s.c", gen_dir, stem);

        /* detect feature flags from source */
        char *test_src = read_file(mxy_files[i]);
        if (strstr(test_src, "Future<") || strstr(test_src, "await "))
            moxy_async_enabled = 1;
        if (strstr(test_src, "[]") || strstr(test_src, "map["))
            moxy_arc_enabled = 1;
        free(test_src);

        info("Transpiling", "%s", base);
        if (transpile_to_file(mxy_files[i], out_path) != 0)
            return -1;
    }

    return 0;
}

static int transpile_project(const Config *cfg) {
    char gen_dir[512];
    snprintf(gen_dir, sizeof(gen_dir), "%s/gen", GOOSE_BUILD);
    return transpile_project_to(cfg, gen_dir);
}

/* ── commands ────────────────────────────────────────────────── */

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* ── single-file commands ────────────────────────────────────── */

static int cmd_run_file(const char *srcpath, int argc, char **argv, int arg_offset) {
    char srcdir[512];
    dir_of(srcpath, srcdir, sizeof(srcdir));
    const char *c_code = transpile(srcpath);

    char tmpdir[] = "/tmp/moxy_XXXXXX";
    if (!mkdtemp(tmpdir)) {
        fprintf(stderr, "moxy: failed to create temp directory\n");
        return 1;
    }

    char cpath[512], binpath[512];
    snprintf(cpath, sizeof(cpath), "%s/out.c", tmpdir);
    snprintf(binpath, sizeof(binpath), "%s/out", tmpdir);

    FILE *f = fopen(cpath, "w");
    if (!f) {
        fprintf(stderr, "moxy: failed to write temp file\n");
        fs_rmrf(tmpdir);
        return 1;
    }
    fputs(c_code, f);
    fclose(f);

    int rc = compile_single(cpath, binpath, srcdir);
    if (rc != 0) {
        fs_rmrf(tmpdir);
        return rc;
    }

    int prog_argc = argc - arg_offset;
    char **prog_argv = malloc(sizeof(char *) * (prog_argc + 2));
    prog_argv[0] = binpath;
    for (int i = 0; i < prog_argc; i++)
        prog_argv[i + 1] = argv[i + arg_offset];
    prog_argv[prog_argc + 1] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        execv(binpath, prog_argv);
        perror("moxy: exec failed");
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);

    free(prog_argv);
    fs_rmrf(tmpdir);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

static int cmd_build_file(const char *srcpath, const char *outpath) {
    char srcdir[512];
    dir_of(srcpath, srcdir, sizeof(srcdir));

    char derived[256];
    if (!outpath) {
        strncpy(derived, srcpath, sizeof(derived) - 1);
        derived[sizeof(derived) - 1] = '\0';
        char *dot = strrchr(derived, '.');
        if (dot) *dot = '\0';
        char *slash = strrchr(derived, '/');
        if (slash) memmove(derived, slash + 1, strlen(slash + 1) + 1);
        outpath = derived;
    }

    const char *c_code = transpile(srcpath);

    char tmpdir[] = "/tmp/moxy_XXXXXX";
    if (!mkdtemp(tmpdir)) {
        fprintf(stderr, "moxy: failed to create temp directory\n");
        return 1;
    }

    char cpath[512];
    snprintf(cpath, sizeof(cpath), "%s/out.c", tmpdir);

    FILE *f = fopen(cpath, "w");
    if (!f) {
        fprintf(stderr, "moxy: failed to write temp file\n");
        fs_rmrf(tmpdir);
        return 1;
    }
    fputs(c_code, f);
    fclose(f);

    int rc = compile_single(cpath, outpath, srcdir);
    fs_rmrf(tmpdir);

    if (rc == 0)
        info("Built", "%s", outpath);
    return rc;
}

/* ── project-mode commands ───────────────────────────────────── */

static int load_project(Config *cfg, LockFile *lf) {
    if (config_load(MOXY_CONFIG, cfg) != 0) {
        err("failed to load %s", MOXY_CONFIG);
        return -1;
    }
    memset(lf, 0, sizeof(*lf));
    lock_load(MOXY_LOCK, lf);
    return 0;
}

/* ── workspace support ──────────────────────────────────────── */

static void ws_adjust_config(Config *cfg, const char *member_dir) {
    char tmp[MAX_PATH_LEN];

    snprintf(tmp, sizeof(tmp), "%s/%s", member_dir, cfg->src_dir);
    strncpy(cfg->src_dir, tmp, MAX_PATH_LEN - 1);

    for (int i = 0; i < cfg->include_count; i++) {
        snprintf(tmp, sizeof(tmp), "%s/%s", member_dir, cfg->includes[i]);
        strncpy(cfg->includes[i], tmp, MAX_PATH_LEN - 1);
    }

    for (int i = 0; i < cfg->dep_count; i++) {
        if (cfg->deps[i].path[0]) {
            snprintf(tmp, sizeof(tmp), "%s/%s", member_dir, cfg->deps[i].path);
            strncpy(cfg->deps[i].path, tmp, MAX_PATH_LEN - 1);
        }
    }
}

static int ws_find_member(const Config members[], int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (strcmp(members[i].name, name) == 0) return i;
    return -1;
}

static int ws_topo_visit(int idx, const Config members[], int count,
                          int *visited, int *order, int *order_count) {
    if (visited[idx] == 2) return 0;
    if (visited[idx] == 1) {
        err("circular dependency involving '%s'", members[idx].name);
        return -1;
    }
    visited[idx] = 1;

    for (int d = 0; d < members[idx].dep_count; d++) {
        int dep_idx = ws_find_member(members, count, members[idx].deps[d].name);
        if (dep_idx >= 0) {
            if (ws_topo_visit(dep_idx, members, count, visited, order, order_count) != 0)
                return -1;
        }
    }

    visited[idx] = 2;
    order[(*order_count)++] = idx;
    return 0;
}

static void ws_collect_deps(int idx, const Config members[], int n,
                             int *needed, int *need_count) {
    for (int k = 0; k < *need_count; k++)
        if (needed[k] == idx) return;

    for (int d = 0; d < members[idx].dep_count; d++) {
        int dep_idx = ws_find_member(members, n, members[idx].deps[d].name);
        if (dep_idx >= 0)
            ws_collect_deps(dep_idx, members, n, needed, need_count);
    }

    needed[(*need_count)++] = idx;
}

static int build_workspace(int release, const char *target) {
    Config root;
    LockFile lf;
    if (load_project(&root, &lf) != 0) return 1;

    int n = root.ws_member_count;
    if (n == 0) {
        err("no workspace members defined");
        return 1;
    }

    static Config members[MAX_WS_MEMBERS];
    memset(members, 0, sizeof(members));
    for (int i = 0; i < n; i++) {
        char cfg_path[512];
        snprintf(cfg_path, sizeof(cfg_path), "%s/%s", root.ws_members[i], MOXY_CONFIG);
        if (config_load(cfg_path, &members[i]) != 0) {
            err("failed to load %s", cfg_path);
            return 1;
        }
        ws_adjust_config(&members[i], root.ws_members[i]);
    }

    /* fetch external deps */
    for (int i = 0; i < n; i++) {
        if (members[i].dep_count > 0)
            pkg_fetch_all(&members[i], &lf);
    }
    lock_save(MOXY_LOCK, &lf);

    /* determine build order */
    int build_set[MAX_WS_MEMBERS];
    int build_count = 0;

    if (target) {
        int tidx = ws_find_member(members, n, target);
        if (tidx < 0) {
            err("workspace member '%s' not found", target);
            return 1;
        }
        ws_collect_deps(tidx, members, n, build_set, &build_count);
    } else {
        int visited[MAX_WS_MEMBERS] = {0};
        for (int i = 0; i < n; i++) {
            if (ws_topo_visit(i, members, n, visited, build_set, &build_count) != 0)
                return 1;
        }
    }

    /* create gen parent directory */
    fs_mkdir(GOOSE_BUILD);
    char gen_parent[512];
    snprintf(gen_parent, sizeof(gen_parent), "%s/gen", GOOSE_BUILD);
    fs_mkdir(gen_parent);

    /* build each member in dependency order */
    for (int i = 0; i < build_count; i++) {
        int idx = build_set[i];
        int is_lib = strcmp(members[idx].type, "lib") == 0;

        info("Building", "%s (%s)", members[idx].name,
             is_lib ? "library" : "binary");

        char gen_dir[512];
        snprintf(gen_dir, sizeof(gen_dir), "%s/gen/%s", GOOSE_BUILD, members[idx].name);
        fs_mkdir(gen_dir);

        if (transpile_project_to(&members[idx], gen_dir) != 0)
            return 1;

        if (is_lib) {
            if (build_library(&members[idx], release, gen_dir) != 0)
                return 1;
        } else {
            /* inject -L/-l and include paths from workspace library deps */
            char lib_dir[512];
            snprintf(lib_dir, sizeof(lib_dir), "%s/lib", GOOSE_BUILD);

            int loff = (int)strlen(members[idx].ldflags);
            for (int d = 0; d < members[idx].dep_count; d++) {
                int dep_idx = ws_find_member(members, n, members[idx].deps[d].name);
                if (dep_idx >= 0 && strcmp(members[dep_idx].type, "lib") == 0) {
                    loff += snprintf(members[idx].ldflags + loff, 256 - loff,
                                    "%s-L%s -l%s", loff > 0 ? " " : "",
                                    lib_dir, members[dep_idx].name);

                    for (int j = 0; j < members[dep_idx].include_count; j++) {
                        if (members[idx].include_count < MAX_INCLUDES) {
                            strncpy(members[idx].includes[members[idx].include_count],
                                    members[dep_idx].includes[j], MAX_PATH_LEN - 1);
                            members[idx].include_count++;
                        }
                    }
                }
            }

            if (build_project_at(&members[idx], release, gen_dir) != 0)
                return 1;
        }
    }

    return 0;
}

static int cmd_build_project(int release, const char *target) {
    Config cfg;
    LockFile lf;
    if (load_project(&cfg, &lf) != 0) return 1;

    if (cfg.ws_member_count > 0)
        return build_workspace(release, target);

    if (cfg.dep_count > 0) {
        pkg_fetch_all(&cfg, &lf);
        lock_save(MOXY_LOCK, &lf);
    }

    if (transpile_project(&cfg) != 0) return 1;
    if (build_project(&cfg, release) != 0) return 1;

    return 0;
}

static int cmd_run_project(int release, const char *target, int argc, char **argv, int arg_offset) {
    Config cfg;
    LockFile lf;
    if (load_project(&cfg, &lf) != 0) return 1;

    /* workspace mode: determine which binary to run */
    char ws_target[MAX_NAME_LEN] = {0};
    if (cfg.ws_member_count > 0) {
        if (target) {
            strncpy(ws_target, target, MAX_NAME_LEN - 1);
        } else {
            int bin_count = 0;
            for (int i = 0; i < cfg.ws_member_count; i++) {
                char mpath[512];
                snprintf(mpath, sizeof(mpath), "%s/%s", cfg.ws_members[i], MOXY_CONFIG);
                Config mcfg;
                if (config_load(mpath, &mcfg) == 0 && strcmp(mcfg.type, "lib") != 0) {
                    strncpy(ws_target, mcfg.name, MAX_NAME_LEN - 1);
                    bin_count++;
                }
            }
            if (bin_count == 0) {
                err("no binary members in workspace");
                return 1;
            }
            if (bin_count > 1) {
                err("multiple binary members; use -p <name> to select");
                return 1;
            }
        }

        if (build_workspace(release, ws_target) != 0) return 1;

        char binpath[512];
        snprintf(binpath, sizeof(binpath), "%s/%s/%s",
                 GOOSE_BUILD, release ? "release" : "debug", ws_target);

        int prog_argc = argc - arg_offset;
        char **prog_argv = malloc(sizeof(char *) * (prog_argc + 2));
        prog_argv[0] = binpath;
        for (int i = 0; i < prog_argc; i++)
            prog_argv[i + 1] = argv[i + arg_offset];
        prog_argv[prog_argc + 1] = NULL;

        pid_t pid = fork();
        if (pid == 0) {
            execv(binpath, prog_argv);
            perror("moxy: exec failed");
            _exit(127);
        }

        int status;
        waitpid(pid, &status, 0);
        free(prog_argv);

        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return 1;
    }

    /* non-workspace mode */
    if (cmd_build_project(release, NULL) != 0) return 1;

    char binpath[512];
    snprintf(binpath, sizeof(binpath), "%s/%s/%s",
             GOOSE_BUILD, release ? "release" : "debug", cfg.name);

    int prog_argc = argc - arg_offset;
    char **prog_argv = malloc(sizeof(char *) * (prog_argc + 2));
    prog_argv[0] = binpath;
    for (int i = 0; i < prog_argc; i++)
        prog_argv[i + 1] = argv[i + arg_offset];
    prog_argv[prog_argc + 1] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        execv(binpath, prog_argv);
        perror("moxy: exec failed");
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    free(prog_argv);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

/* ── command dispatchers ─────────────────────────────────────── */

static int cmd_run(int argc, char **argv) {
    int release = 0;
    const char *target = NULL;
    int arg_idx = 2;

    /* scan for --release/-r and -p before file arg */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--release") == 0 || strcmp(argv[i], "-r") == 0) {
            release = 1;
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j + 1];
            argc--; i--;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            target = argv[i + 1];
            for (int j = i; j < argc - 2; j++) argv[j] = argv[j + 2];
            argc -= 2; i--;
        }
    }

    /* if next arg is a .mxy file → single-file mode */
    if (argc > 2 && ends_with(argv[2], ".mxy"))
        return cmd_run_file(argv[2], argc, argv, 3);

    /* project mode */
    if (!is_project_mode()) {
        fprintf(stderr, "usage: moxy run <file.mxy> [args]\n");
        fprintf(stderr, "   or: moxy run [--release] [-p member] (in a project with %s)\n", MOXY_CONFIG);
        return 1;
    }

    return cmd_run_project(release, target, argc, argv, arg_idx);
}

static int cmd_build(int argc, char **argv) {
    int release = 0;
    const char *outpath = NULL;
    const char *target = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--release") == 0 || strcmp(argv[i], "-r") == 0) {
            release = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outpath = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            target = argv[++i];
        }
    }

    /* if a .mxy file is specified → single-file mode */
    if (argc > 2 && ends_with(argv[2], ".mxy"))
        return cmd_build_file(argv[2], outpath);

    /* project mode */
    if (!is_project_mode()) {
        fprintf(stderr, "usage: moxy build <file.mxy> [-o out]\n");
        fprintf(stderr, "   or: moxy build [--release] [-p member] (in a project with %s)\n", MOXY_CONFIG);
        return 1;
    }

    return cmd_build_project(release, target);
}

/* ── test ────────────────────────────────────────────────────── */

static int run_one_test(const char *srcpath) {
    char *test_src = read_file(srcpath);
    int needs_async = (strstr(test_src, "Future<") != NULL ||
                       strstr(test_src, "await ") != NULL);
    int needs_arc = (strstr(srcpath, "arc") != NULL &&
                     (strstr(test_src, "[]") != NULL ||
                      strstr(test_src, "map[") != NULL));
    free(test_src);

    int saved_async = moxy_async_enabled;
    int saved_arc = moxy_arc_enabled;
    if (needs_async) moxy_async_enabled = 1;
    if (needs_arc) moxy_arc_enabled = 1;

    const char *c_code = transpile(srcpath);

    char tmpdir[] = "/tmp/moxy_XXXXXX";
    if (!mkdtemp(tmpdir)) return 1;

    char cpath[512], binpath[512];
    snprintf(cpath, sizeof(cpath), "%s/out.c", tmpdir);
    snprintf(binpath, sizeof(binpath), "%s/out", tmpdir);

    FILE *f = fopen(cpath, "w");
    if (!f) { fs_rmrf(tmpdir); return 1; }
    fputs(c_code, f);
    fclose(f);

    char testdir[512];
    dir_of(srcpath, testdir, sizeof(testdir));
    int rc = compile_single(cpath, binpath, testdir);
    if (rc != 0) { fs_rmrf(tmpdir); return rc; }

    pid_t pid = fork();
    if (pid == 0) {
        execl(binpath, binpath, (char *)NULL);
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    fs_rmrf(tmpdir);

    moxy_async_enabled = saved_async;
    moxy_arc_enabled = saved_arc;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

static void collect_files(const char *dir, const char *suffix, char files[][512], int *count, int max) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *count < max) {
        if (ent->d_name[0] == '.') continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        if (ent->d_type == DT_DIR) {
            collect_files(path, suffix, files, count, max);
        } else if (ends_with(ent->d_name, suffix)) {
            strncpy(files[*count], path, 511);
            files[*count][511] = '\0';
            (*count)++;
        }
    }
    closedir(d);
}

static int cmd_test(int argc, char **argv) {
    char files[256][512];
    int nfiles = 0;

    if (argc >= 3) {
        for (int i = 2; i < argc && nfiles < 256; i++) {
            strncpy(files[nfiles], argv[i], 511);
            files[nfiles][511] = '\0';
            nfiles++;
        }
    } else {
        collect_files(".", "_test.mxy", files, &nfiles, 256);
    }

    if (nfiles == 0) {
        fprintf(stderr, "moxy: no test files found\n");
        fprintf(stderr, "  name test files with _test.mxy suffix (e.g. math_test.mxy)\n");
        return 1;
    }

    int passed = 0, failed = 0;
    double total_ms = 0;

    for (int i = 0; i < nfiles; i++) {
        const char *display = files[i];
        if (display[0] == '.' && display[1] == '/') display += 2;

        fprintf(stderr, "  test %s ... ", display);
        fflush(stderr);

        double start = now_ms();
        int rc = run_one_test(files[i]);
        double elapsed = now_ms() - start;
        total_ms += elapsed;

        if (rc == 0) {
            fprintf(stderr, "ok (%.0fms)\n", elapsed);
            passed++;
        } else {
            fprintf(stderr, "FAIL (exit %d, %.0fms)\n", rc, elapsed);
            failed++;
        }
    }

    fprintf(stderr, "\n  %d passed, %d failed (%d total) in %.1fs\n",
            passed, failed, nfiles, total_ms / 1000.0);

    return failed > 0 ? 1 : 0;
}

/* ── fmt / lint ──────────────────────────────────────────────── */

static MoxyConfig load_config_for(const char *filepath) {
    char filedir[512];
    dir_of(filepath, filedir, sizeof(filedir));
    char *cfgpath = mxyconf_find(".", filedir);
    MoxyConfig cfg;
    if (cfgpath) {
        cfg = mxyconf_load(cfgpath);
        free(cfgpath);
    } else {
        cfg = mxyconf_defaults();
    }
    return cfg;
}

static int cmd_fmt(int argc, char **argv) {
    int check_only = 0;
    char files[256][512];
    int nfiles = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) {
            check_only = 1;
        } else if (nfiles < 256) {
            strncpy(files[nfiles], argv[i], 511);
            files[nfiles][511] = '\0';
            nfiles++;
        }
    }

    if (nfiles == 0)
        collect_files(".", ".mxy", files, &nfiles, 256);

    if (nfiles == 0) {
        fprintf(stderr, "moxy: no .mxy files found\n");
        return 1;
    }

    int any_diff = 0;
    for (int i = 0; i < nfiles; i++) {
        char *src = read_file(files[i]);
        MoxyConfig cfg = load_config_for(files[i]);

        if (check_only) {
            if (fmt_check(src, &cfg)) {
                const char *display = files[i];
                if (display[0] == '.' && display[1] == '/') display += 2;
                fprintf(stderr, "  %s needs formatting\n", display);
                any_diff = 1;
            }
        } else {
            char *formatted = fmt_source(src, &cfg);
            if (strcmp(src, formatted) != 0) {
                FILE *f = fopen(files[i], "w");
                if (f) {
                    fputs(formatted, f);
                    fclose(f);
                    const char *display = files[i];
                    if (display[0] == '.' && display[1] == '/') display += 2;
                    fprintf(stderr, "  formatted %s\n", display);
                }
            }
            free(formatted);
        }
        free(src);
    }

    return any_diff ? 1 : 0;
}

static int cmd_lint(int argc, char **argv) {
    char files[256][512];
    int nfiles = 0;

    for (int i = 2; i < argc && nfiles < 256; i++) {
        strncpy(files[nfiles], argv[i], 511);
        files[nfiles][511] = '\0';
        nfiles++;
    }

    if (nfiles == 0)
        collect_files(".", ".mxy", files, &nfiles, 256);

    if (nfiles == 0) {
        fprintf(stderr, "moxy: no .mxy files found\n");
        return 1;
    }

    int total_warnings = 0;
    for (int i = 0; i < nfiles; i++) {
        char *raw = read_file(files[i]);
        char *src = preprocess(raw, files[i]);
        free(raw);

        diag_init(src, files[i]);

        Lexer lexer;
        lexer_init(&lexer, src);

        Token tokens[16384];
        int ntokens = 0;
        for (;;) {
            tokens[ntokens] = lexer_next(&lexer);
            if (tokens[ntokens].kind == TOK_EOF) { ntokens++; break; }
            ntokens++;
        }

        Node *program = parse(tokens, ntokens);
        MoxyConfig cfg = load_config_for(files[i]);
        int warnings = lint_check(program, &cfg, src, files[i]);
        total_warnings += warnings;

        free(src);
    }

    if (total_warnings > 0) {
        fprintf(stderr, "\n  %d warning%s\n", total_warnings,
                total_warnings == 1 ? "" : "s");
    }

    return total_warnings > 0 ? 1 : 0;
}

/* ── check ───────────────────────────────────────────────────── */

static int cmd_check(int argc, char **argv) {
    char files[256][512];
    int nfiles = 0;

    for (int i = 2; i < argc && nfiles < 256; i++) {
        strncpy(files[nfiles], argv[i], 511);
        files[nfiles][511] = '\0';
        nfiles++;
    }

    if (nfiles == 0)
        collect_files(".", ".mxy", files, &nfiles, 256);

    if (nfiles == 0) {
        fprintf(stderr, "moxy: no .mxy files found\n");
        return 1;
    }

    int checked = 0;
    int errors = 0;

    for (int i = 0; i < nfiles; i++) {
        const char *display = files[i];
        if (display[0] == '.' && display[1] == '/') display += 2;

        /* transpile validates the full pipeline */
        codegen_reset_includes();
        char *raw = read_file(files[i]);
        char *src = preprocess(raw, files[i]);
        free(raw);

        diag_init(src, files[i]);

        Lexer lexer;
        lexer_init(&lexer, src);

        Token tokens[16384];
        int ntokens = 0;
        for (;;) {
            tokens[ntokens] = lexer_next(&lexer);
            if (tokens[ntokens].kind == TOK_EOF) { ntokens++; break; }
            ntokens++;
        }

        Node *program = parse(tokens, ntokens);
        codegen(program);

        free(src);

        fprintf(stderr, "  ok %s\n", display);
        checked++;
    }

    fprintf(stderr, "\n  %d file%s checked, all ok\n", checked,
            checked == 1 ? "" : "s");

    return errors > 0 ? 1 : 0;
}

/* ── package management commands ─────────────────────────────── */

static int cmd_new(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: moxy new <name>\n");
        return 1;
    }

    const char *name = argv[2];

    if (fs_exists(name)) {
        err("directory '%s' already exists", name);
        return 1;
    }

    info("Creating", "%s", name);
    fs_mkdir(name);

    char path[512];

    /* write moxy.yaml */
    Config cfg;
    config_default(&cfg, name);
    snprintf(path, sizeof(path), "%s/%s", name, MOXY_CONFIG);
    config_save(path, &cfg);

    /* write src/main.mxy */
    snprintf(path, sizeof(path), "%s/src", name);
    fs_mkdir(path);

    snprintf(path, sizeof(path), "%s/src/main.mxy", name);
    fs_write_file(path,
        "#include <stdio.h>\n"
        "\n"
        "void main() {\n"
        "    print(\"hello, world\")\n"
        "}\n"
    );

    /* write .gitignore */
    snprintf(path, sizeof(path), "%s/.gitignore", name);
    fs_write_file(path,
        "build/\n"
        "packages/\n"
    );

    info("Created", "project %s", name);
    return 0;
}

static int cmd_init(int argc, char **argv) {
    (void)argc; (void)argv;

    if (fs_exists(MOXY_CONFIG)) {
        err("%s already exists", MOXY_CONFIG);
        return 1;
    }

    /* derive name from current directory */
    char cwd[512];
    if (!getcwd(cwd, sizeof(cwd))) {
        err("cannot get current directory");
        return 1;
    }
    const char *name = strrchr(cwd, '/');
    name = name ? name + 1 : cwd;

    Config cfg;
    config_default(&cfg, name);
    config_save(MOXY_CONFIG, &cfg);

    if (!fs_exists("src")) fs_mkdir("src");

    if (!fs_exists(".gitignore")) {
        fs_write_file(".gitignore",
            "build/\n"
            "packages/\n"
        );
    }

    info("Initialized", "project %s", name);
    return 0;
}

static int cmd_add(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: moxy add <git-url> [--name N] [--version TAG]\n");
        return 1;
    }

    if (!is_project_mode()) {
        err("no %s found", MOXY_CONFIG);
        return 1;
    }

    const char *git_url = argv[2];
    const char *dep_name = NULL;
    const char *dep_version = "";

    for (int i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "--name") == 0) dep_name = argv[++i];
        else if (strcmp(argv[i], "--version") == 0) dep_version = argv[++i];
    }

    if (!dep_name) dep_name = pkg_name_from_git(git_url);

    Config cfg;
    LockFile lf;
    if (load_project(&cfg, &lf) != 0) return 1;

    /* check if already exists */
    for (int i = 0; i < cfg.dep_count; i++) {
        if (strcmp(cfg.deps[i].name, dep_name) == 0) {
            err("dependency '%s' already exists", dep_name);
            return 1;
        }
    }

    if (cfg.dep_count >= MAX_DEPS) {
        err("too many dependencies (max %d)", MAX_DEPS);
        return 1;
    }

    /* add to config */
    Dependency *dep = &cfg.deps[cfg.dep_count++];
    strncpy(dep->name, dep_name, MAX_NAME_LEN - 1);
    strncpy(dep->git, git_url, MAX_PATH_LEN - 1);
    strncpy(dep->version, dep_version, sizeof(dep->version) - 1);

    /* fetch */
    info("Adding", "%s", dep_name);
    if (pkg_fetch(dep, GOOSE_PKG_DIR, &lf) != 0) {
        err("failed to fetch %s", dep_name);
        return 1;
    }

    config_save(MOXY_CONFIG, &cfg);
    lock_save(MOXY_LOCK, &lf);

    info("Added", "%s", dep_name);
    return 0;
}

static int cmd_remove(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: moxy remove <name>\n");
        return 1;
    }

    if (!is_project_mode()) {
        err("no %s found", MOXY_CONFIG);
        return 1;
    }

    const char *name = argv[2];

    Config cfg;
    LockFile lf;
    if (load_project(&cfg, &lf) != 0) return 1;

    /* find and remove from config */
    int found = -1;
    for (int i = 0; i < cfg.dep_count; i++) {
        if (strcmp(cfg.deps[i].name, name) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        err("dependency '%s' not found", name);
        return 1;
    }

    info("Removing", "%s", name);

    /* shift deps down */
    for (int i = found; i < cfg.dep_count - 1; i++)
        cfg.deps[i] = cfg.deps[i + 1];
    cfg.dep_count--;

    pkg_remove(name, GOOSE_PKG_DIR);
    config_save(MOXY_CONFIG, &cfg);

    /* remove from lock file */
    int lfound = -1;
    for (int i = 0; i < lf.count; i++) {
        if (strcmp(lf.entries[i].name, name) == 0) {
            lfound = i;
            break;
        }
    }
    if (lfound >= 0) {
        for (int i = lfound; i < lf.count - 1; i++)
            lf.entries[i] = lf.entries[i + 1];
        lf.count--;
        lock_save(MOXY_LOCK, &lf);
    }

    info("Removed", "%s", name);
    return 0;
}

static int cmd_update(int argc, char **argv) {
    (void)argc; (void)argv;

    if (!is_project_mode()) {
        err("no %s found", MOXY_CONFIG);
        return 1;
    }

    Config cfg;
    LockFile lf;
    if (load_project(&cfg, &lf) != 0) return 1;

    info("Updating", "packages");
    pkg_update_all(&cfg, &lf);
    lock_save(MOXY_LOCK, &lf);

    info("Updated", "%d package%s", cfg.dep_count,
         cfg.dep_count == 1 ? "" : "s");
    return 0;
}

static int cmd_clean(int argc, char **argv) {
    (void)argc; (void)argv;
    return build_clean();
}

static int cmd_install(int argc, char **argv) {
    const char *prefix = "/usr/local";

    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "--prefix") == 0)
            prefix = argv[++i];
    }

    if (!is_project_mode()) {
        err("no %s found", MOXY_CONFIG);
        return 1;
    }

    if (cmd_build_project(1, NULL) != 0) return 1;

    Config cfg;
    config_load(MOXY_CONFIG, &cfg);

    char src[512], dst[512];
    snprintf(src, sizeof(src), "%s/release/%s", GOOSE_BUILD, cfg.name);
    snprintf(dst, sizeof(dst), "%s/bin", prefix);
    fs_mkdir(dst);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "install -m 755 '%s' '%s/%s'", src, dst, cfg.name);
    if (system(cmd) != 0) {
        err("install failed");
        return 1;
    }

    info("Installed", "%s/%s", dst, cfg.name);
    return 0;
}

/* ── usage / main ────────────────────────────────────────────── */

static void print_usage(void) {
    fprintf(stderr,
        "usage: moxy <command> [args]\n"
        "\n"
        "transpile:\n"
        "  <file.mxy>                 transpile to C on stdout\n"
        "  run <file.mxy> [args]      transpile, compile, and execute\n"
        "  build <file.mxy> [-o out]  transpile and compile to binary\n"
        "  test [files...]            discover and run *_test.mxy files\n"
        "\n"
        "project:\n"
        "  new <name>                 create new project\n"
        "  init                       initialize project in current directory\n"
        "  build [--release] [-p member]  build project or workspace member\n"
        "  run [--release] [-p member]    build and run project or member\n"
        "  clean                      remove build directory\n"
        "  install [--prefix PATH]    release build and install\n"
        "\n"
        "packages:\n"
        "  add <git-url> [opts]       add dependency (--name, --version)\n"
        "  remove <name>              remove dependency\n"
        "  update                     update all dependencies\n"
        "\n"
        "tools:\n"
        "  fmt [file.mxy] [--check]   format source files\n"
        "  lint [file.mxy]            lint source files for issues\n"
        "  check [file.mxy]           check syntax without compiling\n"
    );
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    /* parse global flags */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--enable-async") == 0) {
            moxy_async_enabled = 1;
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j + 1];
            argc--; i--;
        } else if (strcmp(argv[i], "--enable-arc") == 0) {
            moxy_arc_enabled = 1;
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j + 1];
            argc--; i--;
        }
    }

    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "run") == 0)     return cmd_run(argc, argv);
    if (strcmp(cmd, "build") == 0)   return cmd_build(argc, argv);
    if (strcmp(cmd, "test") == 0)    return cmd_test(argc, argv);
    if (strcmp(cmd, "fmt") == 0)     return cmd_fmt(argc, argv);
    if (strcmp(cmd, "lint") == 0)    return cmd_lint(argc, argv);
    if (strcmp(cmd, "check") == 0)   return cmd_check(argc, argv);
    if (strcmp(cmd, "new") == 0)     return cmd_new(argc, argv);
    if (strcmp(cmd, "init") == 0)    return cmd_init(argc, argv);
    if (strcmp(cmd, "add") == 0)     return cmd_add(argc, argv);
    if (strcmp(cmd, "remove") == 0)  return cmd_remove(argc, argv);
    if (strcmp(cmd, "update") == 0)  return cmd_update(argc, argv);
    if (strcmp(cmd, "clean") == 0)   return cmd_clean(argc, argv);
    if (strcmp(cmd, "install") == 0) return cmd_install(argc, argv);

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    }

    /* bare .mxy file → transpile to stdout */
    if (ends_with(cmd, ".mxy")) {
        const char *c_code = transpile(cmd);
        printf("%s", c_code);
        return 0;
    }

    fprintf(stderr, "moxy: unknown command '%s'\n", cmd);
    print_usage();
    return 1;
}
