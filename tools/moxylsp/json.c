#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>

/* ---- constructors ---- */

static JsonNode *jn_alloc(void) {
    return (JsonNode *)calloc(1, sizeof(JsonNode));
}

JsonNode *json_null(void)         { JsonNode *n = jn_alloc(); n->type = JN_NULL; return n; }
JsonNode *json_bool(int v)        { JsonNode *n = jn_alloc(); n->type = JN_BOOL; n->bval = v; return n; }
JsonNode *json_number(double v)   { JsonNode *n = jn_alloc(); n->type = JN_NUM;  n->nval = v; return n; }
JsonNode *json_int(int v)         { return json_number((double)v); }

JsonNode *json_string(const char *v) {
    JsonNode *n = jn_alloc();
    n->type = JN_STR;
    n->sval = strdup(v ? v : "");
    return n;
}

JsonNode *json_array(void) {
    JsonNode *n = jn_alloc();
    n->type = JN_ARR;
    n->icap = 8;
    n->items = calloc(8, sizeof(JsonNode *));
    return n;
}

JsonNode *json_object(void) {
    JsonNode *n = jn_alloc();
    n->type = JN_OBJ;
    n->ocap = 8;
    n->keys = calloc(8, sizeof(char *));
    n->vals = calloc(8, sizeof(JsonNode *));
    return n;
}

/* ---- mutators ---- */

void json_array_push(JsonNode *a, JsonNode *v) {
    if (!a || a->type != JN_ARR) return;
    if (a->ilen >= a->icap) {
        a->icap *= 2;
        a->items = realloc(a->items, a->icap * sizeof(JsonNode *));
    }
    a->items[a->ilen++] = v;
}

void json_object_set(JsonNode *o, const char *k, JsonNode *v) {
    if (!o || o->type != JN_OBJ) return;
    for (int i = 0; i < o->olen; i++) {
        if (strcmp(o->keys[i], k) == 0) {
            json_free(o->vals[i]);
            o->vals[i] = v;
            return;
        }
    }
    if (o->olen >= o->ocap) {
        o->ocap *= 2;
        o->keys = realloc(o->keys, o->ocap * sizeof(char *));
        o->vals = realloc(o->vals, o->ocap * sizeof(JsonNode *));
    }
    o->keys[o->olen] = strdup(k);
    o->vals[o->olen] = v;
    o->olen++;
}

/* ---- accessors ---- */

JsonNode *json_object_get(JsonNode *o, const char *k) {
    if (!o || o->type != JN_OBJ) return NULL;
    for (int i = 0; i < o->olen; i++)
        if (strcmp(o->keys[i], k) == 0) return o->vals[i];
    return NULL;
}

JsonNode *json_array_get(JsonNode *a, int i) {
    if (!a || a->type != JN_ARR || i < 0 || i >= a->ilen) return NULL;
    return a->items[i];
}

const char *json_string_val(JsonNode *n) {
    return (n && n->type == JN_STR) ? n->sval : NULL;
}

int json_int_val(JsonNode *n) {
    return (n && n->type == JN_NUM) ? (int)n->nval : 0;
}

int json_array_len(JsonNode *a) {
    return (a && a->type == JN_ARR) ? a->ilen : 0;
}

/* ---- free ---- */

void json_free(JsonNode *n) {
    if (!n) return;
    if (n->type == JN_STR) free(n->sval);
    if (n->type == JN_ARR) {
        for (int i = 0; i < n->ilen; i++) json_free(n->items[i]);
        free(n->items);
    }
    if (n->type == JN_OBJ) {
        for (int i = 0; i < n->olen; i++) {
            free(n->keys[i]);
            json_free(n->vals[i]);
        }
        free(n->keys);
        free(n->vals);
    }
    free(n);
}

/* ---- parser ---- */

static const char *jp;

static void skip_ws(void) {
    while (*jp == ' ' || *jp == '\t' || *jp == '\n' || *jp == '\r') jp++;
}

static JsonNode *pv(void); /* forward: parse_value */

static JsonNode *ps(void) { /* parse string */
    if (*jp != '"') return NULL;
    jp++;
    int cap = 64, len = 0;
    char *buf = malloc(cap);
    while (*jp && *jp != '"') {
        if (len + 8 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        if (*jp == '\\') {
            jp++;
            switch (*jp) {
            case '"':  buf[len++] = '"';  break;
            case '\\': buf[len++] = '\\'; break;
            case '/':  buf[len++] = '/';  break;
            case 'b':  buf[len++] = '\b'; break;
            case 'f':  buf[len++] = '\f'; break;
            case 'n':  buf[len++] = '\n'; break;
            case 'r':  buf[len++] = '\r'; break;
            case 't':  buf[len++] = '\t'; break;
            case 'u':  buf[len++] = '?'; jp += 4; break;
            default:   buf[len++] = *jp; break;
            }
            jp++;
        } else {
            buf[len++] = *jp++;
        }
    }
    if (*jp == '"') jp++;
    buf[len] = '\0';
    JsonNode *n = json_string(buf);
    free(buf);
    return n;
}

static JsonNode *pv(void) {
    skip_ws();
    if (*jp == '"') return ps();
    if (*jp == '{') {
        jp++; skip_ws();
        JsonNode *o = json_object();
        if (*jp == '}') { jp++; return o; }
        while (*jp) {
            skip_ws();
            if (*jp != '"') break;
            JsonNode *k = ps();
            skip_ws();
            if (*jp == ':') jp++;
            skip_ws();
            JsonNode *v = pv();
            json_object_set(o, k->sval, v);
            json_free(k);
            skip_ws();
            if (*jp == ',') { jp++; continue; }
            if (*jp == '}') { jp++; break; }
            break;
        }
        return o;
    }
    if (*jp == '[') {
        jp++; skip_ws();
        JsonNode *a = json_array();
        if (*jp == ']') { jp++; return a; }
        while (*jp) {
            skip_ws();
            json_array_push(a, pv());
            skip_ws();
            if (*jp == ',') { jp++; continue; }
            if (*jp == ']') { jp++; break; }
            break;
        }
        return a;
    }
    if (strncmp(jp, "true", 4) == 0)  { jp += 4; return json_bool(1); }
    if (strncmp(jp, "false", 5) == 0) { jp += 5; return json_bool(0); }
    if (strncmp(jp, "null", 4) == 0)  { jp += 4; return json_null(); }
    if (*jp == '-' || isdigit((unsigned char)*jp)) {
        char *end;
        double v = strtod(jp, &end);
        if (end != jp) { jp = end; return json_number(v); }
    }
    return json_null();
}

JsonNode *json_parse(const char *src) { jp = src; return pv(); }

/* ---- serializer ---- */

typedef struct { char *b; int len, cap; } Buf;

static Buf buf_new(void) {
    Buf b = { malloc(1024), 0, 1024 };
    return b;
}

static void bp(Buf *b, const char *s, int n) {
    while (b->len + n >= b->cap) { b->cap *= 2; b->b = realloc(b->b, b->cap); }
    memcpy(b->b + b->len, s, n);
    b->len += n;
}

static void bs(Buf *b, const char *s) { bp(b, s, (int)strlen(s)); }
static void bc(Buf *b, char c) { bp(b, &c, 1); }

static void ser(Buf *b, JsonNode *n) {
    if (!n) { bs(b, "null"); return; }
    switch (n->type) {
    case JN_NULL: bs(b, "null"); break;
    case JN_BOOL: bs(b, n->bval ? "true" : "false"); break;
    case JN_NUM: {
        char t[64];
        if (n->nval == (double)(int)n->nval && n->nval >= -1e15 && n->nval <= 1e15)
            snprintf(t, 64, "%d", (int)n->nval);
        else
            snprintf(t, 64, "%.17g", n->nval);
        bs(b, t);
        break;
    }
    case JN_STR:
        bc(b, '"');
        for (const char *s = n->sval; *s; s++) {
            switch (*s) {
            case '"':  bs(b, "\\\""); break;
            case '\\': bs(b, "\\\\"); break;
            case '\b': bs(b, "\\b");  break;
            case '\f': bs(b, "\\f");  break;
            case '\n': bs(b, "\\n");  break;
            case '\r': bs(b, "\\r");  break;
            case '\t': bs(b, "\\t");  break;
            default:
                if ((unsigned char)*s < 0x20) {
                    char e[8]; snprintf(e, 8, "\\u%04x", (unsigned char)*s);
                    bs(b, e);
                } else bc(b, *s);
            }
        }
        bc(b, '"');
        break;
    case JN_ARR:
        bc(b, '[');
        for (int i = 0; i < n->ilen; i++) {
            if (i) bc(b, ',');
            ser(b, n->items[i]);
        }
        bc(b, ']');
        break;
    case JN_OBJ:
        bc(b, '{');
        for (int i = 0; i < n->olen; i++) {
            if (i) bc(b, ',');
            bc(b, '"');
            for (const char *s = n->keys[i]; *s; s++) {
                if (*s == '"') bs(b, "\\\"");
                else if (*s == '\\') bs(b, "\\\\");
                else bc(b, *s);
            }
            bc(b, '"');
            bc(b, ':');
            ser(b, n->vals[i]);
        }
        bc(b, '}');
        break;
    }
}

char *json_serialize(JsonNode *n) {
    Buf b = buf_new();
    ser(&b, n);
    bc(&b, '\0');
    return b.b;
}

/* ---- json-rpc send ---- */

void jrpc_send(JsonNode *msg) {
    char *body = json_serialize(msg);
    int len = (int)strlen(body);
    fprintf(stdout, "Content-Length: %d\r\n\r\n%s", len, body);
    fflush(stdout);
    free(body);
}

/* ---- document store ---- */

#define MAX_DOCS 64

typedef struct {
    char uri[2048];
    char *content;
    int version;
} Doc;

static Doc docs[MAX_DOCS];
static int ndocs = 0;

void doc_open(const char *uri, const char *content, int version) {
    for (int i = 0; i < ndocs; i++) {
        if (strcmp(docs[i].uri, uri) == 0) {
            free(docs[i].content);
            docs[i].content = strdup(content);
            docs[i].version = version;
            return;
        }
    }
    if (ndocs >= MAX_DOCS) return;
    snprintf(docs[ndocs].uri, 2048, "%s", uri);
    docs[ndocs].content = strdup(content);
    docs[ndocs].version = version;
    ndocs++;
}

void doc_close(const char *uri) {
    for (int i = 0; i < ndocs; i++) {
        if (strcmp(docs[i].uri, uri) == 0) {
            free(docs[i].content);
            docs[i] = docs[ndocs - 1];
            ndocs--;
            return;
        }
    }
}

const char *doc_content(const char *uri) {
    for (int i = 0; i < ndocs; i++)
        if (strcmp(docs[i].uri, uri) == 0)
            return docs[i].content;
    return NULL;
}

/* ---- diagnostics via moxy check ---- */

JsonNode *run_diagnostics(const char *content, const char *moxy_path) {
    char tmp[256];
    snprintf(tmp, 256, "/tmp/moxylsp_%d.mxy", getpid());
    FILE *f = fopen(tmp, "w");
    if (!f) return json_array();
    fputs(content, f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, 512, "%s check %s 2>&1", moxy_path, tmp);
    FILE *proc = popen(cmd, "r");
    JsonNode *diags = json_array();

    if (proc) {
        char line[1024];
        int err_line = -1, err_col = 0;
        char err_msg[512] = "";

        while (fgets(line, sizeof(line), proc)) {
            if (strncmp(line, "error", 5) == 0) {
                char *c = strchr(line + 5, ':');
                if (c) {
                    c++;
                    while (*c == ' ') c++;
                    char *nl = strchr(c, '\n');
                    if (nl) *nl = '\0';
                    snprintf(err_msg, 512, "%s", c);
                }
            }
            char *arrow = strstr(line, "-->");
            if (arrow) {
                char *loc = arrow + 3;
                while (*loc == ' ') loc++;
                char *lc = strrchr(loc, ':');
                if (lc) {
                    err_col = atoi(lc + 1);
                    *lc = '\0';
                    char *lc2 = strrchr(loc, ':');
                    if (lc2) err_line = atoi(lc2 + 1) - 1;
                }
                if (err_line >= 0 && err_msg[0]) {
                    JsonNode *d = json_object();
                    JsonNode *range = json_object();
                    JsonNode *rs = json_object();
                    json_object_set(rs, "line", json_int(err_line));
                    json_object_set(rs, "character", json_int(err_col > 0 ? err_col - 1 : 0));
                    JsonNode *re = json_object();
                    json_object_set(re, "line", json_int(err_line));
                    json_object_set(re, "character", json_int(err_col > 0 ? err_col + 10 : 80));
                    json_object_set(range, "start", rs);
                    json_object_set(range, "end", re);
                    json_object_set(d, "range", range);
                    json_object_set(d, "severity", json_int(1));
                    json_object_set(d, "source", json_string("moxy"));
                    json_object_set(d, "message", json_string(err_msg));
                    json_array_push(diags, d);
                    err_line = -1;
                    err_msg[0] = '\0';
                }
            }
        }
        pclose(proc);
    }
    unlink(tmp);
    return diags;
}

/* ---- document symbols ---- */

static int is_keyword(const char *w) {
    static const char *kw[] = {
        "if","else","for","while","do","return","match","switch","case",
        "default","break","continue","goto","enum","struct","union",
        "typedef","sizeof","static","const","extern",NULL
    };
    for (int i = 0; kw[i]; i++)
        if (strcmp(w, kw[i]) == 0) return 1;
    return 0;
}

static JsonNode *make_range(int line, int sc, int ec) {
    JsonNode *r = json_object();
    JsonNode *s = json_object();
    json_object_set(s, "line", json_int(line));
    json_object_set(s, "character", json_int(sc));
    JsonNode *e = json_object();
    json_object_set(e, "line", json_int(line));
    json_object_set(e, "character", json_int(ec));
    json_object_set(r, "start", s);
    json_object_set(r, "end", e);
    return r;
}

JsonNode *scan_symbols(const char *content) {
    JsonNode *syms = json_array();
    if (!content) return syms;
    const char *p = content;
    int line = 0;

    while (*p) {
        const char *ls = p;
        while (*p && *p != '\n') p++;
        int ll = (int)(p - ls);
        char buf[512];
        if (ll >= (int)sizeof(buf)) ll = (int)sizeof(buf) - 1;
        memcpy(buf, ls, ll);
        buf[ll] = '\0';

        if (buf[0] != ' ' && buf[0] != '\t' && buf[0] != '#' &&
            buf[0] != '/' && buf[0] != '\0') {
            /* enum? */
            if (strncmp(buf, "enum ", 5) == 0) {
                char *ns = buf + 5;
                while (*ns == ' ') ns++;
                char *ne = ns;
                while (*ne && *ne != ' ' && *ne != '{') ne++;
                int nlen = (int)(ne - ns);
                char name[128];
                if (nlen > 127) nlen = 127;
                memcpy(name, ns, nlen);
                name[nlen] = '\0';
                JsonNode *sym = json_object();
                json_object_set(sym, "name", json_string(name));
                json_object_set(sym, "kind", json_int(10));
                json_object_set(sym, "range", make_range(line, 0, ll));
                json_object_set(sym, "selectionRange", make_range(line, 5, 5 + nlen));
                json_array_push(syms, sym);
            }
            /* function? look for ( */
            else if (strchr(buf, '(')) {
                char *paren = strchr(buf, '(');
                char *ne = paren;
                while (ne > buf && *(ne - 1) == ' ') ne--;
                char *ns = ne;
                while (ns > buf && *(ns - 1) != ' ' && *(ns - 1) != '*') ns--;
                int nlen = (int)(ne - ns);
                if (nlen > 0 && nlen < 128) {
                    char name[128];
                    memcpy(name, ns, nlen);
                    name[nlen] = '\0';
                    if (!is_keyword(name)) {
                        JsonNode *sym = json_object();
                        json_object_set(sym, "name", json_string(name));
                        json_object_set(sym, "kind", json_int(12));
                        json_object_set(sym, "range", make_range(line, 0, ll));
                        json_object_set(sym, "selectionRange",
                            make_range(line, (int)(ns - buf), (int)(ne - buf)));
                        json_array_push(syms, sym);
                    }
                }
            }
        }
        if (*p == '\n') p++;
        line++;
    }
    return syms;
}

/* ---- word at position ---- */

void word_at_pos(const char *content, int line, int col, char *buf, int bufsz) {
    buf[0] = '\0';
    if (!content) return;
    const char *p = content;
    for (int i = 0; i < line && *p; i++) {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    int ll = 0;
    while (p[ll] && p[ll] != '\n') ll++;
    if (col >= ll) return;
    int ws = col, we = col;
    while (ws > 0 && (isalnum((unsigned char)p[ws - 1]) || p[ws - 1] == '_')) ws--;
    while (we < ll && (isalnum((unsigned char)p[we]) || p[we] == '_')) we++;
    int wl = we - ws;
    if (wl <= 0 || wl >= bufsz) return;
    memcpy(buf, p + ws, wl);
    buf[wl] = '\0';
}
