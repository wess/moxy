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
#include "yaml.h"
#include "fmt.h"
#include "lint.h"
#include "flags.h"

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

                char *inc_src = read_file(fullpath);
                char *processed = preprocess(inc_src, fullpath);

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

static const char *transpile(const char *path) {
    char *raw = read_file(path);
    char *src = preprocess(raw, path);
    free(raw);

    diag_init(src, path);

    Lexer lexer;
    lexer_init(&lexer, src);

    Token tokens[4096];
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

static void rmrf(const char *path) {
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    system(cmd);
}

static int compile(const char *cpath, const char *binpath) {
    const char *cc = getenv("CC");
    if (!cc) cc = "cc";

    const char *cflags = getenv("CFLAGS");
    const char *pthread_flag = moxy_async_enabled ? " -lpthread" : "";

    char cmd[2048];
    if (cflags) {
        snprintf(cmd, sizeof(cmd), "%s -std=c11 %s -o '%s' '%s'%s",
                 cc, cflags, binpath, cpath, pthread_flag);
    } else {
        snprintf(cmd, sizeof(cmd), "%s -std=c11 -o '%s' '%s'%s",
                 cc, binpath, cpath, pthread_flag);
    }

    int status = system(cmd);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    return 0;
}

static int cmd_run(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: moxy run <file.mxy> [args...]\n");
        return 1;
    }

    const char *srcpath = argv[2];
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
        rmrf(tmpdir);
        return 1;
    }
    fputs(c_code, f);
    fclose(f);

    int rc = compile(cpath, binpath);
    if (rc != 0) {
        rmrf(tmpdir);
        return rc;
    }

    int prog_argc = argc - 3;
    char **prog_argv = malloc(sizeof(char *) * (prog_argc + 2));
    prog_argv[0] = binpath;
    for (int i = 0; i < prog_argc; i++) {
        prog_argv[i + 1] = argv[i + 3];
    }
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
    rmrf(tmpdir);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

static int cmd_build(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: moxy build <file.mxy> [-o output]\n");
        return 1;
    }

    const char *srcpath = argv[2];
    const char *outpath = NULL;

    for (int i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            outpath = argv[i + 1];
            break;
        }
    }

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
        rmrf(tmpdir);
        return 1;
    }
    fputs(c_code, f);
    fclose(f);

    int rc = compile(cpath, outpath);
    rmrf(tmpdir);

    if (rc == 0) {
        fprintf(stderr, "moxy: built %s\n", outpath);
    }
    return rc;
}

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

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
    if (!f) { rmrf(tmpdir); return 1; }
    fputs(c_code, f);
    fclose(f);

    int rc = compile(cpath, binpath);
    if (rc != 0) { rmrf(tmpdir); return rc; }

    pid_t pid = fork();
    if (pid == 0) {
        execl(binpath, binpath, (char *)NULL);
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    rmrf(tmpdir);

    moxy_async_enabled = saved_async;
    moxy_arc_enabled = saved_arc;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

static void collect_test_files(const char *dir, char files[][512], int *count, int max) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *count < max) {
        if (ent->d_name[0] == '.') continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        if (ent->d_type == DT_DIR) {
            collect_test_files(path, files, count, max);
        } else if (ends_with(ent->d_name, "_test.mxy")) {
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
        collect_test_files(".", files, &nfiles, 256);
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

static MoxyConfig load_config_for(const char *filepath) {
    char filedir[512];
    dir_of(filepath, filedir, sizeof(filedir));
    char *cfgpath = config_find(".", filedir);
    MoxyConfig cfg;
    if (cfgpath) {
        cfg = config_load(cfgpath);
        free(cfgpath);
    } else {
        cfg = config_defaults();
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

    if (nfiles == 0) {
        collect_mxy_files(".", files, &nfiles, 256);
    }

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

    if (nfiles == 0) {
        collect_mxy_files(".", files, &nfiles, 256);
    }

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

        Token tokens[4096];
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

static void print_usage(void) {
    fprintf(stderr,
        "usage: moxy <command> [args]\n"
        "\n"
        "commands:\n"
        "  run <file.mxy> [args]      transpile, compile, and execute\n"
        "  build <file.mxy> [-o out]  transpile and compile to binary\n"
        "  test [files...]            discover and run *_test.mxy files\n"
        "  fmt [file.mxy] [--check]   format source files\n"
        "  lint [file.mxy]            lint source files for issues\n"
        "  <file.mxy>                 transpile to C on stdout\n"
    );
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--enable-async") == 0) {
            moxy_async_enabled = 1;
            for (int j = i; j < argc - 1; j++)
                argv[j] = argv[j + 1];
            argc--;
            i--;
        } else if (strcmp(argv[i], "--enable-arc") == 0) {
            moxy_arc_enabled = 1;
            for (int j = i; j < argc - 1; j++)
                argv[j] = argv[j + 1];
            argc--;
            i--;
        }
    }

    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc, argv);
    }

    if (strcmp(argv[1], "build") == 0) {
        return cmd_build(argc, argv);
    }

    if (strcmp(argv[1], "test") == 0) {
        return cmd_test(argc, argv);
    }

    if (strcmp(argv[1], "fmt") == 0) {
        return cmd_fmt(argc, argv);
    }

    if (strcmp(argv[1], "lint") == 0) {
        return cmd_lint(argc, argv);
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }

    const char *c_code = transpile(argv[1]);
    printf("%s", c_code);
    return 0;
}
