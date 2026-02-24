#include "codegen.h"
#include "flags.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

static char out[262144];
static int outpos;
static int indent;

typedef struct { char name[64]; char type[64]; } Sym;
static Sym syms[256];
static int nsyms;

typedef struct { char name[64]; Variant variants[16]; int nvariants; int simple; } EnumStore;
static EnumStore enums[16];
static int nenums;

static char type_insts[32][64];
static int ninsts;
static int in_main;

static char user_includes[64][256];
static int nuser_includes;

static char user_directives[128][512];
static int nuser_directives;

static int forin_counter;
static int async_counter;
static int has_futures;

static Node *lambdas[64];
static int nlambdas;

typedef struct { char name[64]; char type[64]; } ArcVar;
typedef struct { ArcVar vars[32]; int nvars; } ArcScope;
static ArcScope arc_scopes[16];
static int arc_depth;

static void emit(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    outpos += vsnprintf(out + outpos, sizeof(out) - outpos, fmt, ap);
    va_end(ap);
}

static void emit_indent(void) {
    for (int i = 0; i < indent; i++) emit("    ");
}

static void emitln(const char *fmt, ...) {
    emit_indent();
    va_list ap;
    va_start(ap, fmt);
    outpos += vsnprintf(out + outpos, sizeof(out) - outpos, fmt, ap);
    va_end(ap);
    emit("\n");
}

static void sym_add(const char *name, const char *type) {
    if (nsyms >= 256) return;
    strncpy(syms[nsyms].name, name, 63);
    syms[nsyms].name[63] = '\0';
    strncpy(syms[nsyms].type, type, 63);
    syms[nsyms].type[63] = '\0';
    nsyms++;
}

static const char *sym_type(const char *name) {
    for (int i = nsyms - 1; i >= 0; i--)
        if (strcmp(syms[i].name, name) == 0) return syms[i].type;
    return NULL;
}

static void inst_add(const char *type) {
    for (int i = 0; i < ninsts; i++)
        if (strcmp(type_insts[i], type) == 0) return;
    if (ninsts >= 32) return;
    strncpy(type_insts[ninsts], type, 63);
    type_insts[ninsts][63] = '\0';
    ninsts++;
}

void codegen_add_include(const char *line) {
    for (int i = 0; i < nuser_includes; i++)
        if (strcmp(user_includes[i], line) == 0) return;
    if (nuser_includes >= 64) return;
    strncpy(user_includes[nuser_includes], line, 255);
    user_includes[nuser_includes][255] = '\0';
    nuser_includes++;
}

void codegen_add_directive(const char *line) {
    if (nuser_directives >= 128) return;
    strncpy(user_directives[nuser_directives], line, 511);
    user_directives[nuser_directives][511] = '\0';
    nuser_directives++;
}

void codegen_reset_includes(void) {
    nuser_includes = 0;
    nuser_directives = 0;
}

static int is_list_type(const char *t) {
    int len = (int)strlen(t);
    return len >= 3 && t[len-2] == '[' && t[len-1] == ']';
}

static int is_result_type(const char *t) {
    return strncmp(t, "Result<", 7) == 0;
}

static int is_map_type(const char *t) {
    return strncmp(t, "map[", 4) == 0;
}

static int is_future_type(const char *t) {
    return strncmp(t, "Future<", 7) == 0;
}

static void future_inner(const char *t, char *buf) {
    int end = (int)strlen(t) - 1;
    strncpy(buf, t + 7, end - 7);
    buf[end - 7] = '\0';
}

static void list_elem(const char *t, char *buf) {
    int len = (int)strlen(t);
    strncpy(buf, t, len - 2);
    buf[len - 2] = '\0';
}

static void result_inner(const char *t, char *buf) {
    int end = (int)strlen(t) - 1;
    strncpy(buf, t + 7, end - 7);
    buf[end - 7] = '\0';
}

static void map_key(const char *t, char *buf) {
    const char *start = t + 4;
    const char *comma = strchr(start, ',');
    int len = (int)(comma - start);
    strncpy(buf, start, len);
    buf[len] = '\0';
}

static void map_val(const char *t, char *buf) {
    const char *comma = strchr(t, ',');
    const char *start = comma + 1;
    int end = (int)strlen(t) - 1;
    int len = (int)(t + end - start);
    strncpy(buf, start, len);
    buf[len] = '\0';
}

static const char *c_type_simple(const char *mxy) {
    if (strcmp(mxy, "string") == 0) return "const char*";
    if (strcmp(mxy, "int") == 0) return "int";
    if (strcmp(mxy, "float") == 0) return "float";
    if (strcmp(mxy, "double") == 0) return "double";
    if (strcmp(mxy, "char") == 0) return "char";
    if (strcmp(mxy, "bool") == 0) return "bool";
    if (strcmp(mxy, "long") == 0) return "long";
    if (strcmp(mxy, "short") == 0) return "short";
    if (strcmp(mxy, "void") == 0) return "void";
    return mxy;
}

static void c_type_buf(const char *mxy, char *buf) {
    if (strstr(mxy, "(*)")) {
        strcpy(buf, mxy);
        return;
    }
    if (is_list_type(mxy)) {
        char elem[64];
        list_elem(mxy, elem);
        snprintf(buf, 128, "list_%s", elem);
        return;
    }
    if (is_result_type(mxy)) {
        char inner[64];
        result_inner(mxy, inner);
        snprintf(buf, 128, "Result_%s", inner);
        return;
    }
    if (is_map_type(mxy)) {
        char k[64], v[64];
        map_key(mxy, k);
        map_val(mxy, v);
        snprintf(buf, 128, "map_%s_%s", k, v);
        return;
    }
    if (is_future_type(mxy)) {
        char inner[64];
        future_inner(mxy, inner);
        snprintf(buf, 128, "Future_%s", inner);
        return;
    }
    if (strstr(mxy, "string")) {
        char tmp[128];
        strcpy(tmp, mxy);
        char *sp = strstr(tmp, "string");
        if (sp) {
            char result[128] = {0};
            int prefix_len = (int)(sp - tmp);
            memcpy(result, tmp, prefix_len);
            strcat(result, "const char*");
            strcat(result, sp + 6);
            strcpy(buf, result);
            return;
        }
    }
    if (strchr(mxy, '*') || strchr(mxy, ' ')) {
        strcpy(buf, mxy);
        return;
    }
    strcpy(buf, c_type_simple(mxy));
}

static const char *fmt_for_type(const char *t) {
    if (!t) return "%d";
    if (strcmp(t, "string") == 0) return "%s";
    if (strcmp(t, "int") == 0) return "%d";
    if (strcmp(t, "float") == 0) return "%f";
    if (strcmp(t, "double") == 0) return "%f";
    if (strcmp(t, "char") == 0) return "%c";
    if (strcmp(t, "bool") == 0) return "%d";
    if (strcmp(t, "long") == 0) return "%ld";
    if (strcmp(t, "short") == 0) return "%hd";
    return "%d";
}

static int is_arc_type(const char *t) {
    return moxy_arc_enabled && (is_list_type(t) || is_map_type(t));
}

static void arc_push_scope(void) {
    if (arc_depth < 16) {
        arc_scopes[arc_depth].nvars = 0;
        arc_depth++;
    }
}

static void arc_register_var(const char *name, const char *type) {
    if (arc_depth <= 0) return;
    ArcScope *s = &arc_scopes[arc_depth - 1];
    if (s->nvars < 32) {
        strncpy(s->vars[s->nvars].name, name, 63);
        s->vars[s->nvars].name[63] = '\0';
        strncpy(s->vars[s->nvars].type, type, 63);
        s->vars[s->nvars].type[63] = '\0';
        s->nvars++;
    }
}

static void arc_emit_release(const char *name, const char *type) {
    char tname[128];
    c_type_buf(type, tname);
    emitln("%s_release(%s);", tname, name);
}

static void arc_pop_scope(void) {
    if (arc_depth <= 0) return;
    arc_depth--;
    ArcScope *s = &arc_scopes[arc_depth];
    for (int i = s->nvars - 1; i >= 0; i--)
        arc_emit_release(s->vars[i].name, s->vars[i].type);
}

static void arc_emit_cleanup_all(const char *exclude) {
    for (int d = arc_depth - 1; d >= 0; d--) {
        ArcScope *s = &arc_scopes[d];
        for (int i = s->nvars - 1; i >= 0; i--) {
            if (exclude && strcmp(s->vars[i].name, exclude) == 0) continue;
            arc_emit_release(s->vars[i].name, s->vars[i].type);
        }
    }
}

static const char *infer_type(Node *n);

static const char *fmt_for(Node *expr) {
    if (expr->kind == NODE_EXPR_STRLIT) return "%s";
    if (expr->kind == NODE_EXPR_INTLIT) return "%d";
    if (expr->kind == NODE_EXPR_FLOATLIT) return "%f";
    if (expr->kind == NODE_EXPR_CHARLIT) return "%c";
    if (expr->kind == NODE_EXPR_BOOLLIT) return "%d";
    const char *t = infer_type(expr);
    return fmt_for_type(t);
}

static const char *infer_type(Node *n) {
    switch (n->kind) {
    case NODE_EXPR_INTLIT: return "int";
    case NODE_EXPR_FLOATLIT: return "float";
    case NODE_EXPR_STRLIT: return "string";
    case NODE_EXPR_CHARLIT: return "char";
    case NODE_EXPR_BOOLLIT: return "bool";
    case NODE_EXPR_IDENT: return sym_type(n->ident.name);
    case NODE_EXPR_FIELD:
        if (strcmp(n->field.name, "len") == 0) return "int";
        return NULL;
    case NODE_EXPR_INDEX: {
        const char *tt = infer_type(n->index.target);
        if (tt && is_list_type(tt)) {
            static char elem[64];
            list_elem(tt, elem);
            return elem;
        }
        return NULL;
    }
    case NODE_EXPR_METHOD: {
        const char *tt = infer_type(n->method.target);
        if (tt && is_map_type(tt)) {
            if (strcmp(n->method.name, "get") == 0) {
                static char val[64];
                map_val(tt, val);
                return val;
            }
            if (strcmp(n->method.name, "has") == 0) return "bool";
        }
        return NULL;
    }
    case NODE_EXPR_CALL: return sym_type(n->call.name);
    case NODE_EXPR_BINOP: {
        const char *op = n->binop.op;
        if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
            strcmp(op, "&&") == 0 || strcmp(op, "||") == 0)
            return "bool";
        return infer_type(n->binop.left);
    }
    case NODE_EXPR_PAREN: return infer_type(n->paren.inner);
    case NODE_EXPR_UNARY: return infer_type(n->unary.operand);
    case NODE_EXPR_TERNARY: return infer_type(n->ternary.then_expr);
    case NODE_EXPR_AWAIT: {
        const char *ft = infer_type(n->await_expr.inner);
        if (ft && is_future_type(ft)) {
            static char awbuf[64];
            future_inner(ft, awbuf);
            return awbuf;
        }
        return ft;
    }
    case NODE_EXPR_LAMBDA: {
        char lname[64];
        snprintf(lname, 64, "__moxy_lambda_%d", n->lambda.id);
        return sym_type(lname);
    }
    case NODE_EXPR_CAST:
    case NODE_RAW:
        return NULL;
    default: return NULL;
    }
}

static int is_simple_enum(const char *ename) {
    for (int i = 0; i < nenums; i++)
        if (strcmp(enums[i].name, ename) == 0) return enums[i].simple;
    return 0;
}

static const char *enum_field_type(const char *ename, const char *vname, int idx) {
    for (int i = 0; i < nenums; i++) {
        if (strcmp(enums[i].name, ename) != 0) continue;
        for (int j = 0; j < enums[i].nvariants; j++) {
            if (strcmp(enums[i].variants[j].name, vname) != 0) continue;
            if (idx < enums[i].variants[j].nfields)
                return enums[i].variants[j].fields[idx].type;
        }
    }
    return "int";
}

static const char *enum_field_name(const char *ename, const char *vname, int idx) {
    for (int i = 0; i < nenums; i++) {
        if (strcmp(enums[i].name, ename) != 0) continue;
        for (int j = 0; j < enums[i].nvariants; j++) {
            if (strcmp(enums[i].variants[j].name, vname) != 0) continue;
            if (idx < enums[i].variants[j].nfields)
                return enums[i].variants[j].fields[idx].name;
        }
    }
    return "unknown";
}

static void emit_list_type(const char *mxy_type) {
    char elem[64], celem[64], tname[128];
    list_elem(mxy_type, elem);
    c_type_buf(elem, celem);
    c_type_buf(mxy_type, tname);

    emit("typedef struct {\n");
    if (moxy_arc_enabled) emit("    int _rc;\n");
    emit("    %s *data;\n", celem);
    emit("    int len;\n");
    emit("    int cap;\n");
    emit("} %s;\n\n", tname);

    if (moxy_arc_enabled) {
        emit("static %s *%s_make(%s *init, int n) {\n", tname, tname, celem);
        emit("    %s *l = (%s *)malloc(sizeof(%s));\n", tname, tname, tname);
        emit("    l->_rc = 1;\n");
        emit("    l->cap = n < 8 ? 8 : n;\n");
        emit("    l->data = (%s*)malloc(l->cap * sizeof(%s));\n", celem, celem);
        emit("    l->len = n;\n");
        emit("    if (n > 0) memcpy(l->data, init, n * sizeof(%s));\n", celem);
        emit("    return l;\n");
        emit("}\n\n");
    } else {
        emit("static %s %s_make(%s *init, int n) {\n", tname, tname, celem);
        emit("    %s l;\n", tname);
        emit("    l.cap = n < 8 ? 8 : n;\n");
        emit("    l.data = (%s*)malloc(l.cap * sizeof(%s));\n", celem, celem);
        emit("    l.len = n;\n");
        emit("    if (n > 0) memcpy(l.data, init, n * sizeof(%s));\n", celem);
        emit("    return l;\n");
        emit("}\n\n");
    }

    emit("static void %s_push(%s *l, %s val) {\n", tname, tname, celem);
    emit("    if (l->len >= l->cap) {\n");
    emit("        l->cap = l->cap < 8 ? 8 : l->cap * 2;\n");
    emit("        l->data = (%s*)realloc(l->data, l->cap * sizeof(%s));\n", celem, celem);
    emit("    }\n");
    emit("    l->data[l->len++] = val;\n");
    emit("}\n\n");

    if (moxy_arc_enabled) {
        emit("static void %s_retain(%s *l) { if (l) l->_rc++; }\n", tname, tname);
        emit("static void %s_release(%s *l) {\n", tname, tname);
        emit("    if (l && --l->_rc == 0) { free(l->data); free(l); }\n");
        emit("}\n\n");
    }
}

static void emit_result_type(const char *mxy_type) {
    char inner[64], cinner[64], tname[128];
    result_inner(mxy_type, inner);
    c_type_buf(inner, cinner);
    c_type_buf(mxy_type, tname);

    int inner_arc = is_arc_type(inner);

    emit("typedef enum { %s_Ok, %s_Err } %s_Tag;\n", tname, tname, tname);
    emit("typedef struct {\n");
    emit("    %s_Tag tag;\n", tname);
    emit("    union {\n");
    if (inner_arc)
        emit("        %s *ok;\n", cinner);
    else
        emit("        %s ok;\n", cinner);
    emit("        const char* err;\n");
    emit("    };\n");
    emit("} %s;\n\n", tname);

    if (inner_arc) {
        emit("static void %s_cleanup(%s *r) {\n", tname, tname);
        emit("    if (r->tag == %s_Ok && r->ok) %s_release(r->ok);\n", tname, cinner);
        emit("}\n\n");
    }
}

static void emit_map_type(const char *mxy_type) {
    char k[64], v[64], ck[64], cv[64], tname[128];
    map_key(mxy_type, k);
    map_val(mxy_type, v);
    c_type_buf(k, ck);
    c_type_buf(v, cv);
    c_type_buf(mxy_type, tname);

    int key_is_str = (strcmp(k, "string") == 0);

    emit("typedef struct {\n");
    if (moxy_arc_enabled) emit("    int _rc;\n");
    emit("    struct { %s key; %s val; } *entries;\n", ck, cv);
    emit("    int len;\n");
    emit("    int cap;\n");
    emit("} %s;\n\n", tname);

    if (moxy_arc_enabled) {
        emit("static %s *%s_make(void) {\n", tname, tname);
        emit("    %s *m = (%s *)malloc(sizeof(%s));\n", tname, tname, tname);
        emit("    m->_rc = 1;\n");
        emit("    m->cap = 8;\n");
        emit("    m->entries = malloc(m->cap * sizeof(*m->entries));\n");
        emit("    m->len = 0;\n");
        emit("    return m;\n");
        emit("}\n\n");
    } else {
        emit("static %s %s_make(void) {\n", tname, tname);
        emit("    %s m;\n", tname);
        emit("    m.cap = 8;\n");
        emit("    m.entries = malloc(m.cap * sizeof(*m.entries));\n");
        emit("    m.len = 0;\n");
        emit("    return m;\n");
        emit("}\n\n");
    }

    const char *cmp = key_is_str ? "strcmp(m->entries[i].key, key) == 0" : "m->entries[i].key == key";

    emit("static void %s_set(%s *m, %s key, %s val) {\n", tname, tname, ck, cv);
    emit("    for (int i = 0; i < m->len; i++) {\n");
    emit("        if (%s) { m->entries[i].val = val; return; }\n", cmp);
    emit("    }\n");
    emit("    if (m->len >= m->cap) {\n");
    emit("        m->cap *= 2;\n");
    emit("        m->entries = realloc(m->entries, m->cap * sizeof(*m->entries));\n");
    emit("    }\n");
    emit("    m->entries[m->len].key = key;\n");
    emit("    m->entries[m->len].val = val;\n");
    emit("    m->len++;\n");
    emit("}\n\n");

    emit("static %s %s_get(%s *m, %s key) {\n", cv, tname, tname, ck);
    emit("    for (int i = 0; i < m->len; i++)\n");
    emit("        if (%s) return m->entries[i].val;\n", cmp);
    emit("    return (%s){0};\n", cv);
    emit("}\n\n");

    emit("static bool %s_has(%s *m, %s key) {\n", tname, tname, ck);
    emit("    for (int i = 0; i < m->len; i++)\n");
    emit("        if (%s) return true;\n", cmp);
    emit("    return false;\n");
    emit("}\n\n");

    if (moxy_arc_enabled) {
        emit("static void %s_retain(%s *m) { if (m) m->_rc++; }\n", tname, tname);
        emit("static void %s_release(%s *m) {\n", tname, tname);
        emit("    if (m && --m->_rc == 0) { free(m->entries); free(m); }\n");
        emit("}\n\n");
    }
}

static void emit_future_type(const char *mxy_type) {
    char inner[64], cinner[64], tname[128];
    future_inner(mxy_type, inner);
    c_type_buf(inner, cinner);
    c_type_buf(mxy_type, tname);

    emit("typedef struct { pthread_t thread; %s result; int started; } %s;\n\n",
         strcmp(inner, "void") == 0 ? "int" : cinner, tname);
}

static void gen_expr(Node *n);
static void gen_stmt(Node *n);

static void gen_expr(Node *n) {
    switch (n->kind) {
    case NODE_EXPR_STRLIT:
        emit("\"%s\"", n->strlit.value);
        break;
    case NODE_EXPR_INTLIT:
        emit("%s", n->intlit.text);
        break;
    case NODE_EXPR_FLOATLIT:
        emit("%s", n->floatlit.value);
        break;
    case NODE_EXPR_CHARLIT:
        emit("'%s'", n->charlit.value);
        break;
    case NODE_EXPR_BOOLLIT:
        emit("%s", n->boollit.value ? "true" : "false");
        break;
    case NODE_EXPR_NULL:
        emit("NULL");
        break;
    case NODE_EXPR_IDENT:
        emit("%s", n->ident.name);
        break;
    case NODE_EXPR_PAREN:
        emit("(");
        gen_expr(n->paren.inner);
        emit(")");
        break;
    case NODE_EXPR_BINOP:
        gen_expr(n->binop.left);
        emit(" %s ", n->binop.op);
        gen_expr(n->binop.right);
        break;
    case NODE_EXPR_UNARY:
        if (strcmp(n->unary.op, "p++") == 0) {
            gen_expr(n->unary.operand);
            emit("++");
        } else if (strcmp(n->unary.op, "p--") == 0) {
            gen_expr(n->unary.operand);
            emit("--");
        } else {
            emit("%s", n->unary.op);
            gen_expr(n->unary.operand);
        }
        break;
    case NODE_EXPR_ENUM_INIT: {
        const char *en = n->enum_init.ename;
        const char *vn = n->enum_init.vname;
        if (is_simple_enum(en)) {
            emit("%s_%s", en, vn);
        } else {
            emit("(%s){ .tag = %s_%s", en, en, vn);
            for (int i = 0; i < nenums; i++) {
                if (strcmp(enums[i].name, en) != 0) continue;
                for (int j = 0; j < enums[i].nvariants; j++) {
                    if (strcmp(enums[i].variants[j].name, vn) != 0) continue;
                    Variant *v = &enums[i].variants[j];
                    if (v->nfields > 0) {
                        emit(", .%s = { ", vn);
                        for (int k = 0; k < v->nfields && k < n->enum_init.nargs; k++) {
                            if (k > 0) emit(", ");
                            emit(".%s = ", v->fields[k].name);
                            gen_expr(n->enum_init.args[k]);
                        }
                        emit(" }");
                    }
                }
            }
            emit(" }");
        }
        break;
    }
    case NODE_EXPR_LIST_LIT:
        break;
    case NODE_EXPR_FIELD: {
        const char *ft = infer_type(n->field.target);
        gen_expr(n->field.target);
        if (n->field.is_arrow || (ft && is_arc_type(ft)))
            emit("->%s", n->field.name);
        else
            emit(".%s", n->field.name);
        break;
    }
    case NODE_EXPR_INDEX: {
        const char *tt = infer_type(n->index.target);
        gen_expr(n->index.target);
        if (tt && is_list_type(tt)) {
            if (is_arc_type(tt))
                emit("->data[");
            else
                emit(".data[");
        } else {
            emit("[");
        }
        gen_expr(n->index.idx);
        emit("]");
        break;
    }
    case NODE_EXPR_METHOD: {
        if (n->method.is_arrow) {
            gen_expr(n->method.target);
            emit("->%s(", n->method.name);
            for (int i = 0; i < n->method.nargs; i++) {
                if (i > 0) emit(", ");
                gen_expr(n->method.args[i]);
            }
            emit(")");
        } else {
            const char *tt = NULL;
            if (n->method.target->kind == NODE_EXPR_IDENT)
                tt = sym_type(n->method.target->ident.name);

            char tname[128];
            if (tt) c_type_buf(tt, tname);
            else strcpy(tname, "unknown");

            if (tt && is_arc_type(tt)) {
                emit("%s_%s(", tname, n->method.name);
            } else {
                emit("%s_%s(&", tname, n->method.name);
            }
            gen_expr(n->method.target);
            for (int i = 0; i < n->method.nargs; i++) {
                emit(", ");
                gen_expr(n->method.args[i]);
            }
            emit(")");
        }
        break;
    }
    case NODE_EXPR_CALL:
        emit("%s(", n->call.name);
        for (int i = 0; i < n->call.nargs; i++) {
            if (i > 0) emit(", ");
            gen_expr(n->call.args[i]);
        }
        emit(")");
        break;
    case NODE_EXPR_EMPTY:
    case NODE_EXPR_OK:
    case NODE_EXPR_ERR:
        break;
    case NODE_RAW:
        emit("%s", n->raw.text);
        break;
    case NODE_EXPR_TERNARY:
        gen_expr(n->ternary.cond);
        emit(" ? ");
        gen_expr(n->ternary.then_expr);
        emit(" : ");
        gen_expr(n->ternary.else_expr);
        break;
    case NODE_EXPR_CAST:
        emit("(%s)", n->cast.type_text);
        gen_expr(n->cast.operand);
        break;
    case NODE_EXPR_AWAIT:
        gen_expr(n->await_expr.inner);
        break;
    case NODE_EXPR_LAMBDA:
        emit("__moxy_lambda_%d", n->lambda.id);
        break;
    default:
        break;
    }
}

static void gen_print(Node *n) {
    const char *f = fmt_for(n->print_stmt.arg);
    emit_indent();
    emit("printf(\"%s\\n\", ", f);
    gen_expr(n->print_stmt.arg);
    emit(");\n");
}

static void gen_assert(Node *n) {
    emit_indent();
    emit("if (!(");
    gen_expr(n->assert_stmt.arg);
    emit(")) { fprintf(stderr, \"FAIL: assert at line %d\\n\"); exit(1); }\n",
         n->assert_stmt.line);
}

static void gen_match(Node *n) {
    const char *target_type = sym_type(n->match_stmt.target);

    /* detect if matching a simple enum */
    int simple = 0;
    if (target_type && is_simple_enum(target_type))
        simple = 1;
    if (!simple && n->match_stmt.narms > 0 &&
        n->match_stmt.arms[0].pattern.enum_name[0])
        simple = is_simple_enum(n->match_stmt.arms[0].pattern.enum_name);

    if (simple)
        emitln("switch (%s) {", n->match_stmt.target);
    else
        emitln("switch (%s.tag) {", n->match_stmt.target);
    indent++;

    for (int i = 0; i < n->match_stmt.narms; i++) {
        MatchArm *arm = &n->match_stmt.arms[i];

        if (arm->pattern.enum_name[0] == '\0') {
            char tname[128];
            if (target_type) c_type_buf(target_type, tname);
            else strcpy(tname, "Result_unknown");

            emitln("case %s_%s: {", tname, arm->pattern.variant);
            indent++;

            if (arm->pattern.binding[0] != '\0') {
                int is_ok = (strcmp(arm->pattern.variant, "Ok") == 0);
                const char *fld = is_ok ? "ok" : "err";
                const char *ft;
                if (is_ok && target_type) {
                    char inner[64];
                    result_inner(target_type, inner);
                    ft = inner;
                } else {
                    ft = "string";
                }
                char ct[128];
                c_type_buf(ft, ct);
                emitln("%s %s = %s.%s;", ct, arm->pattern.binding,
                    n->match_stmt.target, fld);
                sym_add(arm->pattern.binding, ft);
            }
        } else {
            emitln("case %s_%s: {", arm->pattern.enum_name, arm->pattern.variant);
            indent++;

            if (arm->pattern.binding[0] != '\0') {
                const char *ft = enum_field_type(
                    arm->pattern.enum_name, arm->pattern.variant, 0);
                const char *fn = enum_field_name(
                    arm->pattern.enum_name, arm->pattern.variant, 0);
                char ct[128];
                c_type_buf(ft, ct);
                emitln("%s %s = %s.%s.%s;", ct, arm->pattern.binding,
                    n->match_stmt.target, arm->pattern.variant, fn);
                sym_add(arm->pattern.binding, ft);
            }
        }

        if (moxy_arc_enabled) arc_push_scope();
        gen_stmt(arm->body);
        if (moxy_arc_enabled) arc_pop_scope();
        emitln("break;");
        indent--;
        emitln("}");
    }

    indent--;
    emitln("}");
}

static void gen_var_decl(Node *n, int is_global) {
    const char *mtype = n->var_decl.type;
    char ct[128];
    c_type_buf(mtype, ct);
    sym_add(n->var_decl.name, mtype);

    if (is_list_type(mtype) || is_result_type(mtype) || is_map_type(mtype))
        inst_add(mtype);

    if (!is_global) emit_indent();

    if (n->var_decl.value->kind == NODE_EXPR_LIST_LIT) {
        Node *lit = n->var_decl.value;
        char elem[64], celem[64];
        list_elem(mtype, elem);
        c_type_buf(elem, celem);
        int arc = is_arc_type(mtype);
        if (lit->list_lit.nitems > 0) {
            emit("%s %s%s = %s_make((%s[]){", ct, arc ? "*" : "", n->var_decl.name, ct, celem);
            for (int i = 0; i < lit->list_lit.nitems; i++) {
                if (i > 0) emit(", ");
                gen_expr(lit->list_lit.items[i]);
            }
            emit("}, %d);\n", lit->list_lit.nitems);
        } else {
            emit("%s %s%s = %s_make(NULL, 0);\n", ct, arc ? "*" : "", n->var_decl.name, ct);
        }
        if (arc) arc_register_var(n->var_decl.name, mtype);
        return;
    }

    if (n->var_decl.value->kind == NODE_EXPR_OK) {
        emit("%s %s = (%s){ .tag = %s_Ok, .ok = ", ct, n->var_decl.name, ct, ct);
        gen_expr(n->var_decl.value->ok_expr.inner);
        emit(" };\n");
        return;
    }
    if (n->var_decl.value->kind == NODE_EXPR_ERR) {
        emit("%s %s = (%s){ .tag = %s_Err, .err = ", ct, n->var_decl.name, ct, ct);
        gen_expr(n->var_decl.value->err_expr.inner);
        emit(" };\n");
        return;
    }

    if (n->var_decl.value->kind == NODE_EXPR_EMPTY && is_map_type(mtype)) {
        if (is_arc_type(mtype)) {
            emit("%s *%s = %s_make();\n", ct, n->var_decl.name, ct);
            arc_register_var(n->var_decl.name, mtype);
        } else {
            emit("%s %s = %s_make();\n", ct, n->var_decl.name, ct);
        }
        return;
    }

    if (n->var_decl.value->kind == NODE_EXPR_AWAIT) {
        Node *inner = n->var_decl.value->await_expr.inner;
        const char *ft = infer_type(inner);
        char fut_inner[64];
        if (ft && is_future_type(ft))
            future_inner(ft, fut_inner);
        else
            strcpy(fut_inner, mtype);

        char fut_ct[128];
        if (ft) c_type_buf(ft, fut_ct);
        else snprintf(fut_ct, 128, "Future_%s", mtype);

        int idx = async_counter++;

        emit("%s _aw%d = ", fut_ct, idx);
        gen_expr(inner);
        emit(";\n");

        if (strcmp(fut_inner, "void") == 0) {
            emitln("pthread_join(_aw%d.thread, NULL);", idx);
        } else if (strcmp(fut_inner, "string") == 0) {
            emitln("void *_aw%d_ret;", idx);
            emitln("pthread_join(_aw%d.thread, &_aw%d_ret);", idx, idx);
            emitln("%s %s = (const char *)_aw%d_ret;", ct, n->var_decl.name, idx);
        } else {
            emitln("void *_aw%d_ret;", idx);
            emitln("pthread_join(_aw%d.thread, &_aw%d_ret);", idx, idx);
            emitln("%s %s = *(%s *)_aw%d_ret;", ct, n->var_decl.name, ct, idx);
            emitln("free(_aw%d_ret);", idx);
        }
        return;
    }

    if (is_arc_type(mtype)) {
        if (n->var_decl.value->kind == NODE_EXPR_IDENT) {
            emitln("%s_retain(%s);", ct, n->var_decl.value->ident.name);
            if (!is_global) emit_indent();
        }
        emit("%s *%s = ", ct, n->var_decl.name);
        gen_expr(n->var_decl.value);
        emit(";\n");
        arc_register_var(n->var_decl.name, mtype);
    } else {
        emit("%s %s = ", ct, n->var_decl.name);
        gen_expr(n->var_decl.value);
        emit(";\n");
    }
}

static void gen_block(Node *block) {
    for (int i = 0; i < block->block.nstmts; i++)
        gen_stmt(block->block.stmts[i]);
}

static void gen_if_inner(Node *n, int is_else_if) {
    if (!is_else_if) emit_indent();
    emit("if (");
    gen_expr(n->if_stmt.cond);
    emit(") {\n");
    indent++;
    if (moxy_arc_enabled) arc_push_scope();
    gen_block(n->if_stmt.then_body);
    if (moxy_arc_enabled) arc_pop_scope();
    indent--;
    if (n->if_stmt.else_body) {
        Node *eb = n->if_stmt.else_body;
        if (eb->block.nstmts == 1 && eb->block.stmts[0]->kind == NODE_IF_STMT) {
            emit_indent();
            emit("} else ");
            gen_if_inner(eb->block.stmts[0], 1);
            return;
        }
        emitln("} else {");
        indent++;
        if (moxy_arc_enabled) arc_push_scope();
        gen_block(n->if_stmt.else_body);
        if (moxy_arc_enabled) arc_pop_scope();
        indent--;
    }
    emitln("}");
}

static void gen_if(Node *n) {
    gen_if_inner(n, 0);
}

static void gen_while(Node *n) {
    emit_indent();
    emit("while (");
    gen_expr(n->while_stmt.cond);
    emit(") {\n");
    indent++;
    if (moxy_arc_enabled) arc_push_scope();
    for (int i = 0; i < n->while_stmt.nbody; i++)
        gen_stmt(n->while_stmt.body[i]);
    if (moxy_arc_enabled) arc_pop_scope();
    indent--;
    emitln("}");
}

static void gen_for_step(Node *n) {
    if (n->kind == NODE_ASSIGN) {
        gen_expr(n->assign.target);
        emit(" %s ", n->assign.op);
        gen_expr(n->assign.value);
    } else if (n->kind == NODE_EXPR_STMT) {
        gen_expr(n->expr_stmt.expr);
    }
}

static void gen_for(Node *n) {
    emit_indent();
    emit("for (");

    if (n->for_stmt.init->kind == NODE_VAR_DECL) {
        const char *mtype = n->for_stmt.init->var_decl.type;
        char ct[128];
        c_type_buf(mtype, ct);
        sym_add(n->for_stmt.init->var_decl.name, mtype);
        emit("%s %s = ", ct, n->for_stmt.init->var_decl.name);
        gen_expr(n->for_stmt.init->var_decl.value);
    } else {
        gen_expr(n->for_stmt.init);
    }
    emit("; ");

    gen_expr(n->for_stmt.cond);
    emit("; ");

    gen_for_step(n->for_stmt.step);
    emit(") {\n");

    indent++;
    if (moxy_arc_enabled) arc_push_scope();
    for (int i = 0; i < n->for_stmt.nbody; i++)
        gen_stmt(n->for_stmt.body[i]);
    if (moxy_arc_enabled) arc_pop_scope();
    indent--;
    emitln("}");
}

static void gen_for_in(Node *n) {
    int idx = forin_counter++;

    if (n->for_in_stmt.iter->kind == NODE_EXPR_RANGE) {
        emit_indent();
        emit("for (int %s = ", n->for_in_stmt.var1);
        gen_expr(n->for_in_stmt.iter->range.start);
        emit("; %s < ", n->for_in_stmt.var1);
        gen_expr(n->for_in_stmt.iter->range.end);
        emit("; %s++) {\n", n->for_in_stmt.var1);
        sym_add(n->for_in_stmt.var1, "int");
        indent++;
        for (int i = 0; i < n->for_in_stmt.nbody; i++)
            gen_stmt(n->for_in_stmt.body[i]);
        indent--;
        emitln("}");
        return;
    }

    const char *coll_name = NULL;
    if (n->for_in_stmt.iter->kind == NODE_EXPR_IDENT)
        coll_name = n->for_in_stmt.iter->ident.name;

    const char *coll_type = coll_name ? sym_type(coll_name) : NULL;

    const char *dot = (coll_type && is_arc_type(coll_type)) ? "->" : ".";

    if (coll_type && is_list_type(coll_type)) {
        char elem[64], celem[64];
        list_elem(coll_type, elem);
        c_type_buf(elem, celem);

        emit_indent();
        emit("for (int _fi%d = 0; _fi%d < ", idx, idx);
        gen_expr(n->for_in_stmt.iter);
        emit("%slen; _fi%d++) {\n", dot, idx);
        indent++;
        if (moxy_arc_enabled) arc_push_scope();
        emit_indent();
        emit("%s %s = ", celem, n->for_in_stmt.var1);
        gen_expr(n->for_in_stmt.iter);
        emit("%sdata[_fi%d];\n", dot, idx);
        sym_add(n->for_in_stmt.var1, elem);
        for (int i = 0; i < n->for_in_stmt.nbody; i++)
            gen_stmt(n->for_in_stmt.body[i]);
        if (moxy_arc_enabled) arc_pop_scope();
        indent--;
        emitln("}");
    } else if (coll_type && is_map_type(coll_type)) {
        char k[64], v[64], ck[64], cv[64];
        map_key(coll_type, k);
        map_val(coll_type, v);
        c_type_buf(k, ck);
        c_type_buf(v, cv);

        emit_indent();
        emit("for (int _fi%d = 0; _fi%d < ", idx, idx);
        gen_expr(n->for_in_stmt.iter);
        emit("%slen; _fi%d++) {\n", dot, idx);
        indent++;
        if (moxy_arc_enabled) arc_push_scope();
        emit_indent();
        emit("%s %s = ", ck, n->for_in_stmt.var1);
        gen_expr(n->for_in_stmt.iter);
        emit("%sentries[_fi%d].key;\n", dot, idx);
        sym_add(n->for_in_stmt.var1, k);

        if (n->for_in_stmt.var2[0] != '\0') {
            emit_indent();
            emit("%s %s = ", cv, n->for_in_stmt.var2);
            gen_expr(n->for_in_stmt.iter);
            emit("%sentries[_fi%d].val;\n", dot, idx);
            sym_add(n->for_in_stmt.var2, v);
        }

        for (int i = 0; i < n->for_in_stmt.nbody; i++)
            gen_stmt(n->for_in_stmt.body[i]);
        if (moxy_arc_enabled) arc_pop_scope();
        indent--;
        emitln("}");
    }
}

static void gen_return(Node *n) {
    if (moxy_arc_enabled && arc_depth > 0) {
        const char *exclude = NULL;
        if (n->return_stmt.value && n->return_stmt.value->kind == NODE_EXPR_IDENT)
            exclude = n->return_stmt.value->ident.name;
        arc_emit_cleanup_all(exclude);
    }
    emit_indent();
    if (n->return_stmt.value) {
        emit("return ");
        gen_expr(n->return_stmt.value);
        emit(";\n");
    } else if (in_main) {
        emit("return 0;\n");
    } else {
        emit("return;\n");
    }
}

static void gen_assign(Node *n) {
    if (strcmp(n->assign.op, "=") == 0 && n->assign.target->kind == NODE_EXPR_IDENT) {
        const char *tt = sym_type(n->assign.target->ident.name);
        if (tt && is_arc_type(tt)) {
            char tname[128];
            c_type_buf(tt, tname);
            emitln("%s_release(%s);", tname, n->assign.target->ident.name);
            emit_indent();
            gen_expr(n->assign.target);
            emit(" = ");
            gen_expr(n->assign.value);
            emit(";\n");
            if (n->assign.value->kind == NODE_EXPR_IDENT) {
                emitln("%s_retain(%s);", tname, n->assign.target->ident.name);
            }
            return;
        }
    }
    emit_indent();
    gen_expr(n->assign.target);
    emit(" %s ", n->assign.op);
    gen_expr(n->assign.value);
    emit(";\n");
}

static void gen_stmt(Node *n) {
    switch (n->kind) {
    case NODE_PRINT_STMT:
        gen_print(n);
        break;
    case NODE_ASSERT_STMT:
        gen_assert(n);
        break;
    case NODE_VAR_DECL:
        gen_var_decl(n, 0);
        break;
    case NODE_MATCH_STMT:
        gen_match(n);
        break;
    case NODE_IF_STMT:
        gen_if(n);
        break;
    case NODE_WHILE_STMT:
        gen_while(n);
        break;
    case NODE_FOR_STMT:
        gen_for(n);
        break;
    case NODE_FOR_IN_STMT:
        gen_for_in(n);
        break;
    case NODE_RETURN_STMT:
        gen_return(n);
        break;
    case NODE_ASSIGN:
        gen_assign(n);
        break;
    case NODE_EXPR_STMT:
        if (n->expr_stmt.expr->kind == NODE_EXPR_AWAIT) {
            Node *inner = n->expr_stmt.expr->await_expr.inner;
            const char *ft = infer_type(inner);
            char fut_inner[64];
            if (ft && is_future_type(ft))
                future_inner(ft, fut_inner);
            else
                strcpy(fut_inner, "void");

            char fut_ct[128];
            if (ft) c_type_buf(ft, fut_ct);
            else strcpy(fut_ct, "Future_void");

            int idx = async_counter++;
            emit_indent();
            emit("%s _aw%d = ", fut_ct, idx);
            gen_expr(inner);
            emit(";\n");

            if (strcmp(fut_inner, "void") == 0) {
                emitln("pthread_join(_aw%d.thread, NULL);", idx);
            } else {
                emitln("void *_aw%d_ret;", idx);
                emitln("pthread_join(_aw%d.thread, &_aw%d_ret);", idx, idx);
                if (strcmp(fut_inner, "string") != 0)
                    emitln("free(_aw%d_ret);", idx);
            }
            break;
        }
        emit_indent();
        gen_expr(n->expr_stmt.expr);
        emit(";\n");
        break;
    case NODE_RAW:
        emitln("%s", n->raw.text);
        break;
    default:
        break;
    }
}

static void gen_enum(Node *n) {
    if (nenums >= 16) return;
    strncpy(enums[nenums].name, n->enum_decl.name, 63);
    enums[nenums].name[63] = '\0';
    enums[nenums].nvariants = n->enum_decl.nvariants;
    for (int i = 0; i < n->enum_decl.nvariants; i++)
        enums[nenums].variants[i] = n->enum_decl.variants[i];

    int has_fields = 0;
    for (int i = 0; i < n->enum_decl.nvariants; i++)
        if (n->enum_decl.variants[i].nfields > 0) has_fields = 1;

    enums[nenums].simple = !has_fields;
    nenums++;

    const char *name = n->enum_decl.name;

    if (!has_fields) {
        /* simple enum: plain typedef enum */
        emit("typedef enum {\n");
        for (int i = 0; i < n->enum_decl.nvariants; i++)
            emit("    %s_%s,\n", name, n->enum_decl.variants[i].name);
        emit("} %s;\n\n", name);
        return;
    }

    /* tagged enum: tag + struct wrapper */
    emit("typedef enum {\n");
    for (int i = 0; i < n->enum_decl.nvariants; i++)
        emit("    %s_%s,\n", name, n->enum_decl.variants[i].name);
    emit("} %s_Tag;\n\n", name);

    emit("typedef struct {\n");
    emit("    %s_Tag tag;\n", name);

    emit("    union {\n");
    for (int i = 0; i < n->enum_decl.nvariants; i++) {
        Variant *v = &n->enum_decl.variants[i];
        if (v->nfields > 0) {
            emit("        struct {");
            for (int j = 0; j < v->nfields; j++) {
                char fct[128];
                c_type_buf(v->fields[j].type, fct);
                emit(" %s %s;", fct, v->fields[j].name);
            }
            emit(" } %s;\n", v->name);
        }
    }
    emit("    };\n");

    emit("} %s;\n\n", name);
}

static void emit_fnptr_param(const char *type, const char *name) {
    /* type is "ret(*)(args)" — insert name: "ret(*name)(args)" */
    const char *star = strstr(type, "(*)");
    if (!star) { emit("%s %s", type, name); return; }
    int prefix = (int)(star - type) + 2; /* up to and including "(*" */
    emit("%.*s%s%s", prefix, type, name, star + 2);
}

static int is_fnptr_type(const char *t) {
    return strstr(t, "(*)") != NULL;
}

static void emit_params(Node *n) {
    if (n->func_decl.nparams == 0) {
        emit("void");
    } else {
        for (int i = 0; i < n->func_decl.nparams; i++) {
            if (i > 0) emit(", ");
            if (strcmp(n->func_decl.params[i].type, "...") == 0) {
                emit("...");
            } else {
                char pct[128];
                c_type_buf(n->func_decl.params[i].type, pct);
                if (is_fnptr_type(pct))
                    emit_fnptr_param(pct, n->func_decl.params[i].name);
                else if (is_arc_type(n->func_decl.params[i].type))
                    emit("%s *%s", pct, n->func_decl.params[i].name);
                else
                    emit("%s %s", pct, n->func_decl.params[i].name);
            }
        }
    }
}

static void gen_forward_decl(Node *n) {
    char retct[128];
    c_type_buf(n->func_decl.ret, retct);
    int is_main = strcmp(n->func_decl.name, "main") == 0;
    if (is_main) return;

    if (is_future_type(n->func_decl.ret)) {
        sym_add(n->func_decl.name, n->func_decl.ret);
        return;
    }

    if (is_arc_type(n->func_decl.ret))
        emit("%s *%s(", retct, n->func_decl.name);
    else
        emit("%s %s(", retct, n->func_decl.name);
    emit_params(n);
    emit(");\n");

    sym_add(n->func_decl.name, n->func_decl.ret);
}

static void gen_async_stmt(Node *n, const char *inner_type) {
    if (n->kind == NODE_RETURN_STMT) {
        if (strcmp(inner_type, "void") == 0) {
            if (n->return_stmt.value)
                gen_stmt(n);
            else
                emitln("return NULL;");
        } else if (strcmp(inner_type, "string") == 0) {
            emit_indent();
            emit("return (void *)");
            if (n->return_stmt.value)
                gen_expr(n->return_stmt.value);
            else
                emit("NULL");
            emit(";\n");
        } else {
            char cinner[128];
            c_type_buf(inner_type, cinner);
            emitln("%s *_ret = malloc(sizeof(%s));", cinner, cinner);
            emit_indent();
            emit("*_ret = ");
            if (n->return_stmt.value)
                gen_expr(n->return_stmt.value);
            else
                emit("0");
            emit(";\n");
            emitln("return (void *)_ret;");
        }
    } else {
        gen_stmt(n);
    }
}

static void gen_async_func(Node *n) {
    const char *fname = n->func_decl.name;
    char inner[64], cinner[128], tname[128];
    future_inner(n->func_decl.ret, inner);
    c_type_buf(inner, cinner);
    c_type_buf(n->func_decl.ret, tname);

    /* 1. args struct */
    emit("typedef struct {");
    if (n->func_decl.nparams == 0) {
        emit(" int _dummy;");
    } else {
        for (int i = 0; i < n->func_decl.nparams; i++) {
            char pct[128];
            c_type_buf(n->func_decl.params[i].type, pct);
            emit(" %s %s;", pct, n->func_decl.params[i].name);
        }
    }
    emit(" } _%s_args;\n\n", fname);

    /* 2. thread function */
    emit("static void *_%s_thread(void *_arg) {\n", fname);
    indent = 1;
    emitln("_%s_args *_a = (_%s_args *)_arg;", fname, fname);
    for (int i = 0; i < n->func_decl.nparams; i++) {
        char pct[128];
        c_type_buf(n->func_decl.params[i].type, pct);
        emitln("%s %s = _a->%s;", pct, n->func_decl.params[i].name,
               n->func_decl.params[i].name);
        sym_add(n->func_decl.params[i].name, n->func_decl.params[i].type);
    }
    emitln("free(_a);");

    for (int i = 0; i < n->func_decl.nbody; i++)
        gen_async_stmt(n->func_decl.body[i], inner);

    int last_is_return = (n->func_decl.nbody > 0 &&
        n->func_decl.body[n->func_decl.nbody - 1]->kind == NODE_RETURN_STMT);
    if (strcmp(inner, "void") == 0 && !last_is_return)
        emitln("return NULL;");

    indent = 0;
    emit("}\n\n");

    /* 3. launcher function */
    emit("static %s %s(", tname, fname);
    emit_params(n);
    emit(") {\n");
    indent = 1;
    emitln("%s _f;", tname);
    emitln("_%s_args *_a = malloc(sizeof(_%s_args));", fname, fname);
    for (int i = 0; i < n->func_decl.nparams; i++)
        emitln("_a->%s = %s;", n->func_decl.params[i].name,
               n->func_decl.params[i].name);
    emitln("pthread_create(&_f.thread, NULL, _%s_thread, _a);", fname);
    emitln("_f.started = 1;");
    emitln("return _f;");
    indent = 0;
    emit("}\n\n");
}

static void gen_func(Node *n) {
    int is_main = strcmp(n->func_decl.name, "main") == 0;

    if (!is_main && is_future_type(n->func_decl.ret)) {
        gen_async_func(n);
        return;
    }

    char retct[128];
    c_type_buf(n->func_decl.ret, retct);

    in_main = is_main;
    if (is_main) {
        emit("int main(void) {\n");
    } else {
        if (is_arc_type(n->func_decl.ret))
            emit("%s *%s(", retct, n->func_decl.name);
        else
            emit("%s %s(", retct, n->func_decl.name);
        emit_params(n);
        emit(") {\n");
    }

    for (int i = 0; i < n->func_decl.nparams; i++) {
        if (strcmp(n->func_decl.params[i].type, "...") != 0)
            sym_add(n->func_decl.params[i].name, n->func_decl.params[i].type);
    }

    indent = 1;

    if (moxy_arc_enabled) {
        arc_push_scope();
        for (int i = 0; i < n->func_decl.nparams; i++) {
            const char *pt = n->func_decl.params[i].type;
            if (is_arc_type(pt)) {
                char pct[128];
                c_type_buf(pt, pct);
                emitln("%s_retain(%s);", pct, n->func_decl.params[i].name);
                arc_register_var(n->func_decl.params[i].name, pt);
            }
        }
    }

    for (int i = 0; i < n->func_decl.nbody; i++)
        gen_stmt(n->func_decl.body[i]);

    if (is_main) {
        if (moxy_arc_enabled) arc_pop_scope();
        emitln("return 0;");
    } else {
        if (moxy_arc_enabled) arc_pop_scope();
    }

    indent = 0;
    emit("}\n\n");
}

static void collect_types(Node *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.ndecls; i++)
            collect_types(n->program.decls[i]);
        break;
    case NODE_VAR_DECL:
        if (is_list_type(n->var_decl.type) ||
            is_result_type(n->var_decl.type) ||
            is_map_type(n->var_decl.type) ||
            is_future_type(n->var_decl.type))
            inst_add(n->var_decl.type);
        collect_types(n->var_decl.value);
        break;
    case NODE_FUNC_DECL:
        if (is_future_type(n->func_decl.ret))
            inst_add(n->func_decl.ret);
        for (int i = 0; i < n->func_decl.nbody; i++)
            collect_types(n->func_decl.body[i]);
        break;
    case NODE_IF_STMT:
        if (n->if_stmt.then_body)
            for (int i = 0; i < n->if_stmt.then_body->block.nstmts; i++)
                collect_types(n->if_stmt.then_body->block.stmts[i]);
        if (n->if_stmt.else_body)
            for (int i = 0; i < n->if_stmt.else_body->block.nstmts; i++)
                collect_types(n->if_stmt.else_body->block.stmts[i]);
        break;
    case NODE_WHILE_STMT:
        for (int i = 0; i < n->while_stmt.nbody; i++)
            collect_types(n->while_stmt.body[i]);
        break;
    case NODE_FOR_STMT:
        collect_types(n->for_stmt.init);
        for (int i = 0; i < n->for_stmt.nbody; i++)
            collect_types(n->for_stmt.body[i]);
        break;
    case NODE_FOR_IN_STMT:
        for (int i = 0; i < n->for_in_stmt.nbody; i++)
            collect_types(n->for_in_stmt.body[i]);
        break;
    case NODE_EXPR_LAMBDA:
        collect_types(n->lambda.body);
        break;
    case NODE_EXPR_STMT:
        collect_types(n->expr_stmt.expr);
        break;
    case NODE_EXPR_CALL:
        for (int i = 0; i < n->call.nargs; i++)
            collect_types(n->call.args[i]);
        break;
    case NODE_RETURN_STMT:
        if (n->return_stmt.value) collect_types(n->return_stmt.value);
        break;
    case NODE_BLOCK:
        for (int i = 0; i < n->block.nstmts; i++)
            collect_types(n->block.stmts[i]);
        break;
    default:
        break;
    }
}

static void collect_lambdas(Node *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.ndecls; i++)
            collect_lambdas(n->program.decls[i]);
        break;
    case NODE_FUNC_DECL:
        for (int i = 0; i < n->func_decl.nbody; i++)
            collect_lambdas(n->func_decl.body[i]);
        break;
    case NODE_VAR_DECL:
        collect_lambdas(n->var_decl.value);
        break;
    case NODE_EXPR_STMT:
        collect_lambdas(n->expr_stmt.expr);
        break;
    case NODE_EXPR_CALL:
        for (int i = 0; i < n->call.nargs; i++)
            collect_lambdas(n->call.args[i]);
        break;
    case NODE_RETURN_STMT:
        if (n->return_stmt.value) collect_lambdas(n->return_stmt.value);
        break;
    case NODE_IF_STMT:
        collect_lambdas(n->if_stmt.cond);
        if (n->if_stmt.then_body)
            for (int i = 0; i < n->if_stmt.then_body->block.nstmts; i++)
                collect_lambdas(n->if_stmt.then_body->block.stmts[i]);
        if (n->if_stmt.else_body)
            for (int i = 0; i < n->if_stmt.else_body->block.nstmts; i++)
                collect_lambdas(n->if_stmt.else_body->block.stmts[i]);
        break;
    case NODE_WHILE_STMT:
        for (int i = 0; i < n->while_stmt.nbody; i++)
            collect_lambdas(n->while_stmt.body[i]);
        break;
    case NODE_FOR_STMT:
        collect_lambdas(n->for_stmt.init);
        for (int i = 0; i < n->for_stmt.nbody; i++)
            collect_lambdas(n->for_stmt.body[i]);
        break;
    case NODE_FOR_IN_STMT:
        for (int i = 0; i < n->for_in_stmt.nbody; i++)
            collect_lambdas(n->for_in_stmt.body[i]);
        break;
    case NODE_BLOCK:
        for (int i = 0; i < n->block.nstmts; i++)
            collect_lambdas(n->block.stmts[i]);
        break;
    case NODE_ASSIGN:
        collect_lambdas(n->assign.value);
        break;
    case NODE_EXPR_BINOP:
        collect_lambdas(n->binop.left);
        collect_lambdas(n->binop.right);
        break;
    case NODE_EXPR_PAREN:
        collect_lambdas(n->paren.inner);
        break;
    case NODE_EXPR_LAMBDA:
        n->lambda.id = nlambdas;
        lambdas[nlambdas++] = n;
        collect_lambdas(n->lambda.body);
        break;
    default:
        break;
    }
}

static int has_include(const char *inc) {
    for (int i = 0; i < nuser_includes; i++)
        if (strcmp(user_includes[i], inc) == 0) return 1;
    return 0;
}

const char *codegen(Node *program) {
    outpos = 0;
    indent = 0;
    nsyms = 0;
    nenums = 0;
    ninsts = 0;
    in_main = 0;
    forin_counter = 0;
    async_counter = 0;
    has_futures = 0;
    nlambdas = 0;
    arc_depth = 0;
    memset(arc_scopes, 0, sizeof(arc_scopes));
    memset(out, 0, sizeof(out));

    collect_types(program);
    collect_lambdas(program);

    for (int i = 0; i < nuser_includes; i++)
        emit("%s\n", user_includes[i]);

    const char *auto_incs[] = {
        "#include <stdlib.h>",
        "#include <stdio.h>",
        "#include <stdbool.h>",
        NULL
    };

    int need_string = 0;
    for (int i = 0; i < ninsts; i++)
        if (is_list_type(type_insts[i]) || is_map_type(type_insts[i]))
            need_string = 1;

    for (int a = 0; auto_incs[a]; a++)
        if (!has_include(auto_incs[a])) emit("%s\n", auto_incs[a]);

    if (need_string && !has_include("#include <string.h>"))
        emit("#include <string.h>\n");

    for (int i = 0; i < ninsts; i++) {
        if (is_future_type(type_insts[i])) { has_futures = 1; break; }
    }
    if (has_futures && !has_include("#include <pthread.h>"))
        emit("#include <pthread.h>\n");
    emit("\n");

    for (int i = 0; i < nuser_directives; i++)
        emit("%s\n", user_directives[i]);

    for (int i = 0; i < program->program.ndecls; i++)
        if (program->program.decls[i]->kind == NODE_ENUM_DECL)
            gen_enum(program->program.decls[i]);

    for (int i = 0; i < ninsts; i++) {
        if (is_list_type(type_insts[i])) emit_list_type(type_insts[i]);
        else if (is_result_type(type_insts[i])) emit_result_type(type_insts[i]);
        else if (is_map_type(type_insts[i])) emit_map_type(type_insts[i]);
        else if (is_future_type(type_insts[i])) emit_future_type(type_insts[i]);
    }

    for (int i = 0; i < program->program.ndecls; i++) {
        if (program->program.decls[i]->kind == NODE_RAW)
            emit("%s\n", program->program.decls[i]->raw.text);
    }

    /* emit lambda functions as static inline */
    for (int i = 0; i < nlambdas; i++) {
        Node *lam = lambdas[i];
        char ret_buf[64];
        strcpy(ret_buf, "void");

        if (lam->lambda.is_expr) {
            int sym_save = nsyms;
            for (int p = 0; p < lam->lambda.nparams; p++)
                sym_add(lam->lambda.params[p].name, lam->lambda.params[p].type);
            const char *r = infer_type(lam->lambda.body);
            strcpy(ret_buf, r ? r : "int");
            nsyms = sym_save;
        } else {
            Node *block = lam->lambda.body;
            int sym_save = nsyms;
            for (int p = 0; p < lam->lambda.nparams; p++)
                sym_add(lam->lambda.params[p].name, lam->lambda.params[p].type);
            for (int s = 0; s < block->block.nstmts; s++) {
                if (block->block.stmts[s]->kind == NODE_RETURN_STMT &&
                    block->block.stmts[s]->return_stmt.value) {
                    const char *r = infer_type(block->block.stmts[s]->return_stmt.value);
                    strcpy(ret_buf, r ? r : "int");
                    break;
                }
            }
            nsyms = sym_save;
        }

        char retct[128];
        c_type_buf(ret_buf, retct);

        emit("static inline %s __moxy_lambda_%d(", retct, lam->lambda.id);
        if (lam->lambda.nparams == 0) {
            emit("void");
        } else {
            for (int p = 0; p < lam->lambda.nparams; p++) {
                if (p > 0) emit(", ");
                char pct[128];
                c_type_buf(lam->lambda.params[p].type, pct);
                emit("%s %s", pct, lam->lambda.params[p].name);
            }
        }
        emit(") {\n");

        int sym_save = nsyms;
        for (int p = 0; p < lam->lambda.nparams; p++)
            sym_add(lam->lambda.params[p].name, lam->lambda.params[p].type);

        indent = 1;
        if (lam->lambda.is_expr) {
            emit_indent();
            emit("return ");
            gen_expr(lam->lambda.body);
            emit(";\n");
        } else {
            for (int s = 0; s < lam->lambda.body->block.nstmts; s++)
                gen_stmt(lam->lambda.body->block.stmts[s]);
        }
        indent = 0;
        emit("}\n\n");

        nsyms = sym_save;

        char lname[64];
        snprintf(lname, 64, "__moxy_lambda_%d", lam->lambda.id);
        sym_add(lname, ret_buf);
    }

    for (int i = 0; i < program->program.ndecls; i++)
        if (program->program.decls[i]->kind == NODE_FUNC_DECL)
            gen_forward_decl(program->program.decls[i]);
    emit("\n");

    for (int i = 0; i < program->program.ndecls; i++) {
        if (program->program.decls[i]->kind != NODE_VAR_DECL) continue;
        gen_var_decl(program->program.decls[i], 1);
    }
    emit("\n");

    for (int i = 0; i < program->program.ndecls; i++)
        if (program->program.decls[i]->kind == NODE_FUNC_DECL)
            gen_func(program->program.decls[i]);

    return out;
}
