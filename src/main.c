#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "token.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"

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

        /* skip leading whitespace for directive check */
        const char *lp = p;
        while (*lp == ' ' || *lp == '\t') lp++;

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
                /* inline the .mxy file */
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
                /* C header — pass through to codegen */
                char directive[300];
                if (is_angle)
                    snprintf(directive, sizeof(directive), "#include <%s>", filename);
                else
                    snprintf(directive, sizeof(directive), "#include \"%s\"", filename);
                codegen_add_include(directive);
            }
        } else {
            /* regular line — copy through */
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: moxy <file.mxy>\n");
        return 1;
    }

    char *raw = read_file(argv[1]);
    char *src = preprocess(raw, argv[1]);
    free(raw);

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

    printf("%s", c_code);

    free(src);
    return 0;
}
