#include "lint.h"
#include "diag.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char name[64];
    int line;
    int col;
    int used;
    int scope;
} LintSym;

static LintSym lsyms[256];
static int nlsyms;
static int scope_depth;
static int warn_count;
static const MoxyConfig *lcfg;

static void lint_push(const char *name, int line, int col) {
    if (nlsyms >= 256) return;
    if (lcfg->lint_shadow_vars) {
        for (int i = nlsyms - 1; i >= 0; i--) {
            if (strcmp(lsyms[i].name, name) == 0 && lsyms[i].scope < scope_depth) {
                char msg[128];
                snprintf(msg, sizeof(msg), "variable '%s' shadows outer declaration", name);
                diag_warn(line, col, msg);
                warn_count++;
                break;
            }
        }
    }
    LintSym *s = &lsyms[nlsyms++];
    strncpy(s->name, name, 63);
    s->name[63] = '\0';
    s->line = line;
    s->col = col;
    s->used = 0;
    s->scope = scope_depth;
}

static void lint_mark_used(const char *name) {
    for (int i = nlsyms - 1; i >= 0; i--) {
        if (strcmp(lsyms[i].name, name) == 0) {
            lsyms[i].used = 1;
            return;
        }
    }
}

static void lint_pop_scope(void) {
    if (!lcfg->lint_unused_vars) {
        while (nlsyms > 0 && lsyms[nlsyms - 1].scope == scope_depth)
            nlsyms--;
        scope_depth--;
        return;
    }
    while (nlsyms > 0 && lsyms[nlsyms - 1].scope == scope_depth) {
        LintSym *s = &lsyms[nlsyms - 1];
        if (!s->used && s->name[0] != '_') {
            char msg[128];
            snprintf(msg, sizeof(msg), "unused variable '%s'", s->name);
            diag_warn(s->line, s->col, msg);
            warn_count++;
        }
        nlsyms--;
    }
    scope_depth--;
}

static void lint_walk(Node *n);

static void lint_walk_expr(Node *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_EXPR_IDENT:
        lint_mark_used(n->ident.name);
        break;
    case NODE_EXPR_BINOP:
        lint_walk_expr(n->binop.left);
        lint_walk_expr(n->binop.right);
        break;
    case NODE_EXPR_UNARY:
        lint_walk_expr(n->unary.operand);
        break;
    case NODE_EXPR_PAREN:
        lint_walk_expr(n->paren.inner);
        break;
    case NODE_EXPR_CALL:
        lint_mark_used(n->call.name);
        for (int i = 0; i < n->call.nargs; i++)
            lint_walk_expr(n->call.args[i]);
        break;
    case NODE_EXPR_METHOD:
        lint_walk_expr(n->method.target);
        for (int i = 0; i < n->method.nargs; i++)
            lint_walk_expr(n->method.args[i]);
        break;
    case NODE_EXPR_FIELD:
        lint_walk_expr(n->field.target);
        break;
    case NODE_EXPR_INDEX:
        lint_walk_expr(n->index.target);
        lint_walk_expr(n->index.idx);
        break;
    case NODE_EXPR_OK:
        lint_walk_expr(n->ok_expr.inner);
        break;
    case NODE_EXPR_ERR:
        lint_walk_expr(n->err_expr.inner);
        break;
    case NODE_EXPR_TERNARY:
        lint_walk_expr(n->ternary.cond);
        lint_walk_expr(n->ternary.then_expr);
        lint_walk_expr(n->ternary.else_expr);
        break;
    case NODE_EXPR_CAST:
        lint_walk_expr(n->cast.operand);
        break;
    case NODE_EXPR_ENUM_INIT:
        for (int i = 0; i < n->enum_init.nargs; i++)
            lint_walk_expr(n->enum_init.args[i]);
        break;
    case NODE_EXPR_LIST_LIT:
        for (int i = 0; i < n->list_lit.nitems; i++)
            lint_walk_expr(n->list_lit.items[i]);
        break;
    case NODE_EXPR_RANGE:
        lint_walk_expr(n->range.start);
        lint_walk_expr(n->range.end);
        break;
    case NODE_EXPR_AWAIT:
        lint_walk_expr(n->await_expr.inner);
        break;
    default:
        break;
    }
}

static void lint_check_empty(Node *n, const char *construct) {
    if (!lcfg->lint_empty_blocks) return;
    char msg[128];
    snprintf(msg, sizeof(msg), "empty %s body", construct);
    diag_warn(n->line, n->col, msg);
    warn_count++;
}

static void lint_walk(Node *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.ndecls; i++)
            lint_walk(n->program.decls[i]);
        break;

    case NODE_FUNC_DECL:
        scope_depth++;
        for (int i = 0; i < n->func_decl.nparams; i++) {
            if (strcmp(n->func_decl.params[i].type, "...") != 0)
                lint_push(n->func_decl.params[i].name, n->line, n->col);
        }
        for (int i = 0; i < n->func_decl.nbody; i++)
            lint_walk(n->func_decl.body[i]);
        lint_pop_scope();
        break;

    case NODE_VAR_DECL:
        lint_walk_expr(n->var_decl.value);
        lint_push(n->var_decl.name, n->line, n->col);
        break;

    case NODE_IF_STMT:
        lint_walk_expr(n->if_stmt.cond);
        if (n->if_stmt.nthen == 0) lint_check_empty(n, "if");
        if (n->if_stmt.then_body) {
            scope_depth++;
            for (int i = 0; i < n->if_stmt.then_body->block.nstmts; i++)
                lint_walk(n->if_stmt.then_body->block.stmts[i]);
            lint_pop_scope();
        }
        if (n->if_stmt.else_body) {
            scope_depth++;
            for (int i = 0; i < n->if_stmt.else_body->block.nstmts; i++)
                lint_walk(n->if_stmt.else_body->block.stmts[i]);
            lint_pop_scope();
        }
        break;

    case NODE_WHILE_STMT:
        lint_walk_expr(n->while_stmt.cond);
        if (n->while_stmt.nbody == 0) lint_check_empty(n, "while");
        scope_depth++;
        for (int i = 0; i < n->while_stmt.nbody; i++)
            lint_walk(n->while_stmt.body[i]);
        lint_pop_scope();
        break;

    case NODE_FOR_STMT:
        scope_depth++;
        lint_walk(n->for_stmt.init);
        lint_walk_expr(n->for_stmt.cond);
        if (n->for_stmt.step) {
            if (n->for_stmt.step->kind == NODE_ASSIGN) {
                lint_walk_expr(n->for_stmt.step->assign.target);
                lint_walk_expr(n->for_stmt.step->assign.value);
            } else if (n->for_stmt.step->kind == NODE_EXPR_STMT) {
                lint_walk_expr(n->for_stmt.step->expr_stmt.expr);
            }
        }
        if (n->for_stmt.nbody == 0) lint_check_empty(n, "for");
        for (int i = 0; i < n->for_stmt.nbody; i++)
            lint_walk(n->for_stmt.body[i]);
        lint_pop_scope();
        break;

    case NODE_FOR_IN_STMT:
        lint_walk_expr(n->for_in_stmt.iter);
        scope_depth++;
        lint_push(n->for_in_stmt.var1, n->line, n->col);
        if (n->for_in_stmt.var2[0])
            lint_push(n->for_in_stmt.var2, n->line, n->col);
        if (n->for_in_stmt.nbody == 0) lint_check_empty(n, "for-in");
        for (int i = 0; i < n->for_in_stmt.nbody; i++)
            lint_walk(n->for_in_stmt.body[i]);
        lint_pop_scope();
        break;

    case NODE_MATCH_STMT:
        lint_mark_used(n->match_stmt.target);
        for (int i = 0; i < n->match_stmt.narms; i++) {
            scope_depth++;
            if (n->match_stmt.arms[i].pattern.binding[0])
                lint_push(n->match_stmt.arms[i].pattern.binding, n->line, n->col);
            lint_walk(n->match_stmt.arms[i].body);
            lint_pop_scope();
        }
        break;

    case NODE_RETURN_STMT:
        lint_walk_expr(n->return_stmt.value);
        break;

    case NODE_PRINT_STMT:
        lint_walk_expr(n->print_stmt.arg);
        break;

    case NODE_ASSERT_STMT:
        lint_walk_expr(n->assert_stmt.arg);
        break;

    case NODE_EXPR_STMT:
        lint_walk_expr(n->expr_stmt.expr);
        break;

    case NODE_ASSIGN:
        lint_walk_expr(n->assign.target);
        lint_walk_expr(n->assign.value);
        break;

    case NODE_BLOCK:
        scope_depth++;
        for (int i = 0; i < n->block.nstmts; i++)
            lint_walk(n->block.stmts[i]);
        lint_pop_scope();
        break;

    case NODE_ENUM_DECL:
    case NODE_RAW:
        break;

    default:
        break;
    }
}

int lint_check(Node *program, const MoxyConfig *cfg, const char *source, const char *filename) {
    nlsyms = 0;
    scope_depth = 0;
    warn_count = 0;
    lcfg = cfg;

    diag_init(source, filename);
    lint_walk(program);

    return warn_count;
}
