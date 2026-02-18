#include "fmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int in_string_or_char(const char *line, int pos) {
    int in_sq = 0, in_dq = 0;
    for (int i = 0; i < pos; i++) {
        if (line[i] == '\\') { i++; continue; }
        if (line[i] == '\'' && !in_dq) in_sq = !in_sq;
        if (line[i] == '"' && !in_sq) in_dq = !in_dq;
    }
    return in_sq || in_dq;
}

static int match_op(const char *s, char *buf) {
    static const char *ops[] = {
        "<<=", ">>=", "...",
        "==", "!=", "<=", ">=", "&&", "||", "<<", ">>",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "|>",
        "++", "--", "->", "=>", "::", "..",
        NULL
    };
    for (int i = 0; ops[i]; i++) {
        int olen = (int)strlen(ops[i]);
        if (strncmp(s, ops[i], olen) == 0) {
            memcpy(buf, ops[i], olen);
            buf[olen] = '\0';
            return olen;
        }
    }
    return 0;
}

static int preceded_by_value(const char *out, int opos) {
    int p = opos - 1;
    while (p >= 0 && out[p] == ' ') p--;
    if (p < 0) return 0;
    char prev = out[p];
    return prev == ')' || prev == ']' || isalnum(prev) || prev == '_' ||
           prev == '"' || prev == '\'';
}

static int is_always_binary(const char *op) {
    return strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
           strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
           strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 ||
           strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0 ||
           strcmp(op, "<<=") == 0 || strcmp(op, ">>=") == 0 ||
           strcmp(op, "+=") == 0 || strcmp(op, "-=") == 0 ||
           strcmp(op, "*=") == 0 || strcmp(op, "/=") == 0 ||
           strcmp(op, "%=") == 0 || strcmp(op, "&=") == 0 ||
           strcmp(op, "|=") == 0 || strcmp(op, "^=") == 0 ||
           strcmp(op, "|>") == 0;
}

static int is_never_spaced(const char *op) {
    return strcmp(op, "++") == 0 || strcmp(op, "--") == 0 ||
           strcmp(op, "->") == 0 || strcmp(op, "::") == 0 ||
           strcmp(op, "..") == 0 || strcmp(op, "...") == 0;
}

static int is_spaced_op(const char *op) {
    return strcmp(op, "=>") == 0;
}

static int is_single_binop(char c) {
    return c == '+' || c == '-' || c == '/' || c == '%' ||
           c == '=' || c == '<' || c == '>' || c == '|' || c == '^';
}

static char *format_line_intra(const char *line, const MoxyConfig *cfg) {
    int len = (int)strlen(line);
    int cap = len * 3 + 16;
    char *out = malloc(cap);
    int opos = 0;

    for (int i = 0; i < len; i++) {
        if (opos + 16 >= cap) {
            cap *= 2;
            out = realloc(out, cap);
        }

        char c = line[i];

        if (c == '"' || c == '\'') {
            char quote = c;
            out[opos++] = c;
            i++;
            while (i < len && line[i] != quote) {
                if (line[i] == '\\' && i + 1 < len) {
                    out[opos++] = line[i++];
                }
                out[opos++] = line[i++];
            }
            if (i < len) out[opos++] = line[i];
            continue;
        }

        if (c == '/' && i + 1 < len && (line[i + 1] == '/' || line[i + 1] == '*')) {
            if (opos > 0 && out[opos - 1] != ' ') out[opos++] = ' ';
            while (i < len) out[opos++] = line[i++];
            break;
        }

        if (c == ',' && cfg->space_after_comma) {
            while (opos > 0 && out[opos - 1] == ' ') opos--;
            out[opos++] = ',';
            if (i + 1 < len && line[i + 1] != '\0') out[opos++] = ' ';
            while (i + 1 < len && line[i + 1] == ' ') i++;
            continue;
        }

        if (c == ';') {
            while (opos > 0 && out[opos - 1] == ' ') opos--;
            out[opos++] = ';';
            continue;
        }

        if (c == '.' && (i + 1 >= len || line[i + 1] != '.')) {
            while (opos > 0 && out[opos - 1] == ' ') opos--;
            out[opos++] = '.';
            while (i + 1 < len && line[i + 1] == ' ') i++;
            continue;
        }

        if (cfg->space_around_ops && !in_string_or_char(line, i)) {
            char opbuf[4];
            int oplen = match_op(line + i, opbuf);

            if (oplen > 0 && is_never_spaced(opbuf)) {
                if (strcmp(opbuf, "->") == 0) {
                    while (opos > 0 && out[opos - 1] == ' ') opos--;
                }
                for (int j = 0; j < oplen; j++) out[opos++] = opbuf[j];
                i += oplen - 1;
                if (strcmp(opbuf, "->") == 0) {
                    while (i + 1 < len && line[i + 1] == ' ') i++;
                }
                continue;
            }

            if (oplen > 0 && (is_always_binary(opbuf) || is_spaced_op(opbuf))) {
                if (opos > 0 && out[opos - 1] != ' ') out[opos++] = ' ';
                for (int j = 0; j < oplen; j++) out[opos++] = opbuf[j];
                i += oplen - 1;
                while (i + 1 < len && line[i + 1] == ' ') i++;
                if (i + 1 < len && line[i + 1] != '\0')
                    out[opos++] = ' ';
                continue;
            }

            if (oplen == 0 && (c == '*' || c == '&')) {
                if (preceded_by_value(out, opos)) {
                    if (opos > 0 && out[opos - 1] != ' ') out[opos++] = ' ';
                    out[opos++] = c;
                    while (i + 1 < len && line[i + 1] == ' ') i++;
                    if (i + 1 < len && line[i + 1] != '\0' &&
                        line[i + 1] != ')' && line[i + 1] != ';' && line[i + 1] != ',')
                        out[opos++] = ' ';
                    continue;
                }
                out[opos++] = c;
                continue;
            }

            if (oplen == 0 && c == '!') {
                out[opos++] = c;
                continue;
            }

            if (oplen == 0 && c == '~') {
                out[opos++] = c;
                continue;
            }

            if (oplen == 0 && is_single_binop(c)) {
                if (preceded_by_value(out, opos)) {
                    if (opos > 0 && out[opos - 1] != ' ') out[opos++] = ' ';
                    out[opos++] = c;
                    while (i + 1 < len && line[i + 1] == ' ') i++;
                    if (i + 1 < len && line[i + 1] != '\0' &&
                        line[i + 1] != ')' && line[i + 1] != ';')
                        out[opos++] = ' ';
                    continue;
                }
                out[opos++] = c;
                continue;
            }
        }

        out[opos++] = c;
    }

    out[opos] = '\0';
    return out;
}

static int starts_with_keyword(const char *trimmed) {
    static const char *kws[] = {
        "if", "else", "for", "while", "return", "match", "switch",
        "case", "default", "do", NULL
    };
    for (int i = 0; kws[i]; i++) {
        int klen = (int)strlen(kws[i]);
        if (strncmp(trimmed, kws[i], klen) == 0 &&
            (trimmed[klen] == '(' || trimmed[klen] == ' ' || trimmed[klen] == '{'))
            return klen;
    }
    return 0;
}

static void ensure_keyword_space(char *line, const MoxyConfig *cfg) {
    if (!cfg->space_after_keyword) return;
    int kwlen = starts_with_keyword(line);
    if (kwlen > 0 && line[kwlen] == '(') {
        int len = (int)strlen(line);
        memmove(line + kwlen + 1, line + kwlen, len - kwlen + 1);
        line[kwlen] = ' ';
    }
}

char *fmt_source(const char *src, const MoxyConfig *cfg) {
    int cap = (int)strlen(src) * 2 + 1024;
    char *out = malloc(cap);
    int opos = 0;
    int depth = 0;
    int in_block_comment = 0;

    const char *p = src;
    while (*p) {
        const char *eol = strchr(p, '\n');
        int linelen = eol ? (int)(eol - p) : (int)strlen(p);

        char *raw = malloc(linelen + 1);
        memcpy(raw, p, linelen);
        raw[linelen] = '\0';

        char *trimmed = raw;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (in_block_comment) {
            char *end = strstr(trimmed, "*/");
            if (end) in_block_comment = 0;
            int indent_sz = cfg->indent * depth;
            while (opos + indent_sz + linelen + 4 >= cap) {
                cap *= 2;
                out = realloc(out, cap);
            }
            for (int i = 0; i < indent_sz; i++) out[opos++] = ' ';
            int tlen = (int)strlen(trimmed);
            memcpy(out + opos, trimmed, tlen);
            opos += tlen;
            out[opos++] = '\n';
            free(raw);
            p += linelen;
            if (eol) p++;
            continue;
        }

        if (trimmed[0] == '#') {
            int tlen = (int)strlen(trimmed);
            while (opos + tlen + 4 >= cap) {
                cap *= 2;
                out = realloc(out, cap);
            }
            memcpy(out + opos, trimmed, tlen);
            opos += tlen;
            out[opos++] = '\n';
            free(raw);
            p += linelen;
            if (eol) p++;
            continue;
        }

        if (strstr(trimmed, "/*") && !strstr(trimmed, "*/")) {
            in_block_comment = 1;
        }

        if (trimmed[0] == '}') depth--;
        if (depth < 0) depth = 0;

        char *formatted = format_line_intra(trimmed, cfg);
        ensure_keyword_space(formatted, cfg);

        int indent_sz = cfg->indent * depth;
        int flen = (int)strlen(formatted);

        if (cfg->max_line_length > 0 && indent_sz + flen > cfg->max_line_length) {
            fprintf(stderr, "warning: line exceeds max_line_length (%d)\n",
                    cfg->max_line_length);
        }

        while (opos + indent_sz + flen + 4 >= cap) {
            cap *= 2;
            out = realloc(out, cap);
        }

        if (flen > 0) {
            for (int i = 0; i < indent_sz; i++) out[opos++] = ' ';
            memcpy(out + opos, formatted, flen);
            opos += flen;
        }
        out[opos++] = '\n';

        int check_len = (int)strlen(trimmed);
        for (int i = check_len - 1; i >= 0; i--) {
            if (trimmed[i] == ' ' || trimmed[i] == '\t') continue;
            if (trimmed[i] == '{') depth++;
            break;
        }

        free(formatted);
        free(raw);
        p += linelen;
        if (eol) p++;
    }

    if (cfg->trailing_newline) {
        if (opos > 0 && out[opos - 1] != '\n') out[opos++] = '\n';
    } else {
        while (opos > 1 && out[opos - 1] == '\n' && out[opos - 2] == '\n')
            opos--;
    }

    out[opos] = '\0';
    return out;
}

int fmt_check(const char *src, const MoxyConfig *cfg) {
    char *formatted = fmt_source(src, cfg);
    int differs = strcmp(src, formatted) != 0;
    free(formatted);
    return differs;
}
