#include "codegen.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

static char out[131072];
static int outpos;
static int indent;

typedef struct { char name[64]; char type[64]; } Sym;
static Sym syms[256];
static int nsyms;

typedef struct { char name[64]; Variant variants[16]; int nvariants; } EnumStore;
static EnumStore enums[16];
static int nenums;

static char type_insts[32][64];
static int ninsts;

static char user_includes[64][256];
static int nuser_includes;

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
    strcpy(syms[nsyms].name, name);
    strcpy(syms[nsyms].type, type);
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
    strcpy(type_insts[ninsts++], type);
}

void codegen_add_include(const char *line) {
    for (int i = 0; i < nuser_includes; i++)
        if (strcmp(user_includes[i], line) == 0) return;
    strncpy(user_includes[nuser_includes], line, 255);
    user_includes[nuser_includes][255] = '\0';
    nuser_includes++;
}

/* ---- type helpers ---- */

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

static void mangle(const char *mxy, char *buf) {
    if (strcmp(mxy, "string") == 0) { strcpy(buf, "string"); return; }
    strcpy(buf, mxy);
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
    if (is_list_type(mxy)) {
        char elem[64], mg[64];
        list_elem(mxy, elem);
        mangle(elem, mg);
        snprintf(buf, 128, "list_%s", mg);
        return;
    }
    if (is_result_type(mxy)) {
        char inner[64], mg[64];
        result_inner(mxy, inner);
        mangle(inner, mg);
        snprintf(buf, 128, "Result_%s", mg);
        return;
    }
    if (is_map_type(mxy)) {
        char k[64], v[64], mk[64], mv[64];
        map_key(mxy, k);
        map_val(mxy, v);
        mangle(k, mk);
        mangle(v, mv);
        snprintf(buf, 128, "map_%s_%s", mk, mv);
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
    default: return NULL;
    }
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

/* ---- generic type codegen ---- */

static void emit_list_type(const char *mxy_type) {
    char elem[64], celem[64], tname[128];
    list_elem(mxy_type, elem);
    c_type_buf(elem, celem);
    c_type_buf(mxy_type, tname);

    emit("typedef struct {\n");
    emit("    %s *data;\n", celem);
    emit("    int len;\n");
    emit("    int cap;\n");
    emit("} %s;\n\n", tname);

    emit("static %s %s_make(%s *init, int n) {\n", tname, tname, celem);
    emit("    %s l;\n", tname);
    emit("    l.cap = n < 8 ? 8 : n;\n");
    emit("    l.data = (%s*)malloc(l.cap * sizeof(%s));\n", celem, celem);
    emit("    l.len = n;\n");
    emit("    if (n > 0) memcpy(l.data, init, n * sizeof(%s));\n", celem);
    emit("    return l;\n");
    emit("}\n\n");

    emit("static void %s_push(%s *l, %s val) {\n", tname, tname, celem);
    emit("    if (l->len >= l->cap) {\n");
    emit("        l->cap = l->cap < 8 ? 8 : l->cap * 2;\n");
    emit("        l->data = (%s*)realloc(l->data, l->cap * sizeof(%s));\n", celem, celem);
    emit("    }\n");
    emit("    l->data[l->len++] = val;\n");
    emit("}\n\n");
}

static void emit_result_type(const char *mxy_type) {
    char inner[64], cinner[64], tname[128];
    result_inner(mxy_type, inner);
    c_type_buf(inner, cinner);
    c_type_buf(mxy_type, tname);

    emit("typedef enum { %s_Ok, %s_Err } %s_Tag;\n", tname, tname, tname);
    emit("typedef struct {\n");
    emit("    %s_Tag tag;\n", tname);
    emit("    union {\n");
    emit("        %s ok;\n", cinner);
    emit("        const char* err;\n");
    emit("    };\n");
    emit("} %s;\n\n", tname);
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
    emit("    struct { %s key; %s val; } *entries;\n", ck, cv);
    emit("    int len;\n");
    emit("    int cap;\n");
    emit("} %s;\n\n", tname);

    emit("static %s %s_make(void) {\n", tname, tname);
    emit("    %s m;\n", tname);
    emit("    m.cap = 8;\n");
    emit("    m.entries = malloc(m.cap * sizeof(*m.entries));\n");
    emit("    m.len = 0;\n");
    emit("    return m;\n");
    emit("}\n\n");

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
}

/* ---- expression codegen ---- */

static void gen_expr(Node *n);
static void gen_stmt(Node *n);

static void gen_expr(Node *n) {
    switch (n->kind) {
    case NODE_EXPR_STRLIT:
        emit("\"%s\"", n->strlit.value);
        break;
    case NODE_EXPR_INTLIT:
        emit("%d", n->intlit.value);
        break;
    case NODE_EXPR_FLOATLIT:
        emit("%s", n->floatlit.value);
        break;
    case NODE_EXPR_CHARLIT:
        emit("'%c'", n->charlit.value);
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
        break;
    }
    case NODE_EXPR_LIST_LIT:
        break;
    case NODE_EXPR_FIELD:
        gen_expr(n->field.target);
        emit(".%s", n->field.name);
        break;
    case NODE_EXPR_INDEX:
        gen_expr(n->index.target);
        emit(".data[");
        gen_expr(n->index.idx);
        emit("]");
        break;
    case NODE_EXPR_METHOD: {
        const char *tt = NULL;
        if (n->method.target->kind == NODE_EXPR_IDENT)
            tt = sym_type(n->method.target->ident.name);

        char tname[128];
        if (tt) c_type_buf(tt, tname);
        else strcpy(tname, "unknown");

        emit("%s_%s(&", tname, n->method.name);
        gen_expr(n->method.target);
        for (int i = 0; i < n->method.nargs; i++) {
            emit(", ");
            gen_expr(n->method.args[i]);
        }
        emit(")");
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
    default:
        break;
    }
}

/* ---- statement codegen ---- */

static void gen_print(Node *n) {
    const char *f = fmt_for(n->print_stmt.arg);
    emit_indent();
    emit("printf(\"%s\\n\", ", f);
    gen_expr(n->print_stmt.arg);
    emit(");\n");
}

static void gen_match(Node *n) {
    const char *target_type = sym_type(n->match_stmt.target);

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

        gen_stmt(arm->body);
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
        if (lit->list_lit.nitems > 0) {
            emit("%s %s = %s_make((%s[]){", ct, n->var_decl.name, ct, celem);
            for (int i = 0; i < lit->list_lit.nitems; i++) {
                if (i > 0) emit(", ");
                gen_expr(lit->list_lit.items[i]);
            }
            emit("}, %d);\n", lit->list_lit.nitems);
        } else {
            emit("%s %s = %s_make(NULL, 0);\n", ct, n->var_decl.name, ct);
        }
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
        emit("%s %s = %s_make();\n", ct, n->var_decl.name, ct);
        return;
    }

    emit("%s %s = ", ct, n->var_decl.name);
    gen_expr(n->var_decl.value);
    emit(";\n");
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
    gen_block(n->if_stmt.then_body);
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
        gen_block(n->if_stmt.else_body);
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
    for (int i = 0; i < n->while_stmt.nbody; i++)
        gen_stmt(n->while_stmt.body[i]);
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
    for (int i = 0; i < n->for_stmt.nbody; i++)
        gen_stmt(n->for_stmt.body[i]);
    indent--;
    emitln("}");
}

static void gen_return(Node *n) {
    emit_indent();
    if (n->return_stmt.value) {
        emit("return ");
        gen_expr(n->return_stmt.value);
        emit(";\n");
    } else {
        emit("return;\n");
    }
}

static void gen_assign(Node *n) {
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
    case NODE_RETURN_STMT:
        gen_return(n);
        break;
    case NODE_ASSIGN:
        gen_assign(n);
        break;
    case NODE_EXPR_STMT:
        emit_indent();
        gen_expr(n->expr_stmt.expr);
        emit(";\n");
        break;
    default:
        break;
    }
}

/* ---- top-level codegen ---- */

static void gen_enum(Node *n) {
    strcpy(enums[nenums].name, n->enum_decl.name);
    enums[nenums].nvariants = n->enum_decl.nvariants;
    for (int i = 0; i < n->enum_decl.nvariants; i++)
        enums[nenums].variants[i] = n->enum_decl.variants[i];
    nenums++;

    const char *name = n->enum_decl.name;

    emit("typedef enum {\n");
    for (int i = 0; i < n->enum_decl.nvariants; i++)
        emit("    %s_%s,\n", name, n->enum_decl.variants[i].name);
    emit("} %s_Tag;\n\n", name);

    emit("typedef struct {\n");
    emit("    %s_Tag tag;\n", name);

    int has_fields = 0;
    for (int i = 0; i < n->enum_decl.nvariants; i++)
        if (n->enum_decl.variants[i].nfields > 0) has_fields = 1;

    if (has_fields) {
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
    }

    emit("} %s;\n\n", name);
}

/* forward declarations for user-defined functions */
static void gen_forward_decl(Node *n) {
    char retct[128];
    c_type_buf(n->func_decl.ret, retct);
    int is_main = strcmp(n->func_decl.name, "main") == 0;
    if (is_main) return;

    emit("%s %s(", retct, n->func_decl.name);
    if (n->func_decl.nparams == 0) {
        emit("void");
    } else {
        for (int i = 0; i < n->func_decl.nparams; i++) {
            if (i > 0) emit(", ");
            char pct[128];
            c_type_buf(n->func_decl.params[i].type, pct);
            emit("%s %s", pct, n->func_decl.params[i].name);
        }
    }
    emit(");\n");

    /* register function name with its return type for inference */
    sym_add(n->func_decl.name, n->func_decl.ret);
}

static void gen_func(Node *n) {
    int is_main = strcmp(n->func_decl.name, "main") == 0;
    char retct[128];
    c_type_buf(n->func_decl.ret, retct);

    if (is_main) {
        emit("int main(void) {\n");
    } else {
        emit("%s %s(", retct, n->func_decl.name);
        if (n->func_decl.nparams == 0) {
            emit("void");
        } else {
            for (int i = 0; i < n->func_decl.nparams; i++) {
                if (i > 0) emit(", ");
                char pct[128];
                c_type_buf(n->func_decl.params[i].type, pct);
                emit("%s %s", pct, n->func_decl.params[i].name);
            }
        }
        emit(") {\n");
    }

    /* add params to symbol table */
    for (int i = 0; i < n->func_decl.nparams; i++)
        sym_add(n->func_decl.params[i].name, n->func_decl.params[i].type);

    indent = 1;
    for (int i = 0; i < n->func_decl.nbody; i++)
        gen_stmt(n->func_decl.body[i]);

    if (is_main)
        emitln("return 0;");

    indent = 0;
    emit("}\n\n");
}

/* ---- first pass: collect type instantiations ---- */

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
            is_map_type(n->var_decl.type))
            inst_add(n->var_decl.type);
        collect_types(n->var_decl.value);
        break;
    case NODE_FUNC_DECL:
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
    default:
        break;
    }
}

const char *codegen(Node *program) {
    outpos = 0;
    indent = 0;
    nsyms = 0;
    nenums = 0;
    ninsts = 0;
    memset(out, 0, sizeof(out));

    collect_types(program);

    /* emit user-specified includes first */
    for (int i = 0; i < nuser_includes; i++)
        emit("%s\n", user_includes[i]);

    /* auto-generated includes, skip if user already provided them */
    const char *auto_incs[] = {
        "#include <stdio.h>",
        "#include <stdbool.h>",
        NULL
    };

    int need_alloc = 0;
    for (int i = 0; i < ninsts; i++)
        if (is_list_type(type_insts[i]) || is_map_type(type_insts[i]))
            need_alloc = 1;

    const char *alloc_incs[] = {
        "#include <stdlib.h>",
        "#include <string.h>",
        NULL
    };

    for (int a = 0; auto_incs[a]; a++) {
        int dup = 0;
        for (int u = 0; u < nuser_includes; u++)
            if (strcmp(user_includes[u], auto_incs[a]) == 0) { dup = 1; break; }
        if (!dup) emit("%s\n", auto_incs[a]);
    }

    if (need_alloc) {
        for (int a = 0; alloc_incs[a]; a++) {
            int dup = 0;
            for (int u = 0; u < nuser_includes; u++)
                if (strcmp(user_includes[u], alloc_incs[a]) == 0) { dup = 1; break; }
            if (!dup) emit("%s\n", alloc_incs[a]);
        }
    }
    emit("\n");

    /* enums first (user-defined) */
    for (int i = 0; i < program->program.ndecls; i++)
        if (program->program.decls[i]->kind == NODE_ENUM_DECL)
            gen_enum(program->program.decls[i]);

    /* generic type instantiations */
    for (int i = 0; i < ninsts; i++) {
        if (is_list_type(type_insts[i])) emit_list_type(type_insts[i]);
        else if (is_result_type(type_insts[i])) emit_result_type(type_insts[i]);
        else if (is_map_type(type_insts[i])) emit_map_type(type_insts[i]);
    }

    /* forward declarations for functions */
    for (int i = 0; i < program->program.ndecls; i++)
        if (program->program.decls[i]->kind == NODE_FUNC_DECL)
            gen_forward_decl(program->program.decls[i]);
    emit("\n");

    /* global variables */
    for (int i = 0; i < program->program.ndecls; i++) {
        if (program->program.decls[i]->kind != NODE_VAR_DECL) continue;
        gen_var_decl(program->program.decls[i], 1);
    }
    emit("\n");

    /* functions */
    for (int i = 0; i < program->program.ndecls; i++)
        if (program->program.decls[i]->kind == NODE_FUNC_DECL)
            gen_func(program->program.decls[i]);

    return out;
}
