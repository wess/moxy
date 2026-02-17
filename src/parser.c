#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Token *toks;
static int pos;

static Token peek(void) { return toks[pos]; }

static Token eat(TokenKind kind) {
    Token t = toks[pos];
    if (t.kind != kind) {
        fprintf(stderr, "moxy: expected token %d, got %d ('%s') at %d:%d\n",
                kind, t.kind, t.text, t.line, t.col);
        exit(1);
    }
    pos++;
    return t;
}

static Token advance(void) { return toks[pos++]; }

static int is_type_start(Token t) {
    return t.kind == TOK_STRING_KW || t.kind == TOK_INT_KW ||
           t.kind == TOK_FLOAT_KW || t.kind == TOK_DOUBLE_KW ||
           t.kind == TOK_CHAR_KW || t.kind == TOK_BOOL_KW ||
           t.kind == TOK_LONG_KW || t.kind == TOK_SHORT_KW ||
           t.kind == TOK_VOID_KW || t.kind == TOK_RESULT_KW ||
           t.kind == TOK_MAP_KW || t.kind == TOK_IDENT;
}

static void parse_type(char *buf) {
    Token t = advance();

    if (t.kind == TOK_RESULT_KW) {
        eat(TOK_LT);
        char inner[64];
        parse_type(inner);
        eat(TOK_GT);
        snprintf(buf, 64, "Result<%s>", inner);
        return;
    }

    if (t.kind == TOK_MAP_KW) {
        eat(TOK_LBRACKET);
        char key[64];
        parse_type(key);
        eat(TOK_COMMA);
        char val[64];
        parse_type(val);
        eat(TOK_RBRACKET);
        snprintf(buf, 64, "map[%s,%s]", key, val);
        return;
    }

    strcpy(buf, t.text);

    if (peek().kind == TOK_LBRACKET &&
        toks[pos + 1].kind == TOK_RBRACKET) {
        eat(TOK_LBRACKET);
        eat(TOK_RBRACKET);
        char base[64];
        strcpy(base, buf);
        snprintf(buf, 64, "%s[]", base);
    }
}

static Node *parse_expr(void);
static Node *parse_expr_prec(int min_prec);
static Node *parse_stmt(void);

/* ---- operator precedence ---- */

static int binop_prec(TokenKind k) {
    switch (k) {
    case TOK_OR:      return 1;
    case TOK_AND:     return 2;
    case TOK_EQEQ:
    case TOK_NEQ:     return 3;
    case TOK_LT:
    case TOK_GT:
    case TOK_LTEQ:
    case TOK_GTEQ:    return 4;
    case TOK_PLUS:
    case TOK_MINUS:   return 5;
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT: return 6;
    default:          return -1;
    }
}

static const char *binop_str(TokenKind k) {
    switch (k) {
    case TOK_PLUS:    return "+";
    case TOK_MINUS:   return "-";
    case TOK_STAR:    return "*";
    case TOK_SLASH:   return "/";
    case TOK_PERCENT: return "%";
    case TOK_EQEQ:   return "==";
    case TOK_NEQ:     return "!=";
    case TOK_LT:      return "<";
    case TOK_GT:      return ">";
    case TOK_LTEQ:    return "<=";
    case TOK_GTEQ:    return ">=";
    case TOK_AND:     return "&&";
    case TOK_OR:      return "||";
    default:          return "?";
    }
}

/* ---- primary expressions ---- */

static Node *parse_primary(void) {
    Token t = peek();

    if (t.kind == TOK_LPAREN) {
        advance();
        Node *n = node_new(NODE_EXPR_PAREN);
        n->paren.inner = parse_expr();
        eat(TOK_RPAREN);
        return n;
    }

    if (t.kind == TOK_STRLIT) {
        advance();
        Node *n = node_new(NODE_EXPR_STRLIT);
        strcpy(n->strlit.value, t.text);
        return n;
    }

    if (t.kind == TOK_INTLIT) {
        advance();
        Node *n = node_new(NODE_EXPR_INTLIT);
        n->intlit.value = atoi(t.text);
        return n;
    }

    if (t.kind == TOK_FLOATLIT) {
        advance();
        Node *n = node_new(NODE_EXPR_FLOATLIT);
        strcpy(n->floatlit.value, t.text);
        return n;
    }

    if (t.kind == TOK_CHARLIT) {
        advance();
        Node *n = node_new(NODE_EXPR_CHARLIT);
        n->charlit.value = t.text[0];
        return n;
    }

    if (t.kind == TOK_TRUE_KW || t.kind == TOK_FALSE_KW) {
        advance();
        Node *n = node_new(NODE_EXPR_BOOLLIT);
        n->boollit.value = (t.kind == TOK_TRUE_KW) ? 1 : 0;
        return n;
    }

    if (t.kind == TOK_NULL_KW) {
        advance();
        return node_new(NODE_EXPR_NULL);
    }

    if (t.kind == TOK_OK_KW) {
        advance();
        eat(TOK_LPAREN);
        Node *n = node_new(NODE_EXPR_OK);
        n->ok_expr.inner = parse_expr();
        eat(TOK_RPAREN);
        return n;
    }

    if (t.kind == TOK_ERR_KW) {
        advance();
        eat(TOK_LPAREN);
        Node *n = node_new(NODE_EXPR_ERR);
        n->err_expr.inner = parse_expr();
        eat(TOK_RPAREN);
        return n;
    }

    if (t.kind == TOK_LBRACKET) {
        advance();
        Node *n = node_new(NODE_EXPR_LIST_LIT);
        n->list_lit.nitems = 0;
        while (peek().kind != TOK_RBRACKET) {
            n->list_lit.items[n->list_lit.nitems++] = parse_expr();
            if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
        }
        eat(TOK_RBRACKET);
        return n;
    }

    if (t.kind == TOK_LBRACE) {
        advance();
        eat(TOK_RBRACE);
        return node_new(NODE_EXPR_EMPTY);
    }

    /* unary operators */
    if (t.kind == TOK_BANG || t.kind == TOK_MINUS) {
        advance();
        Node *n = node_new(NODE_EXPR_UNARY);
        strcpy(n->unary.op, t.text);
        n->unary.operand = parse_primary();
        return n;
    }

    if (t.kind == TOK_IDENT) {
        Token name = advance();

        /* enum constructor: Name::Variant(...) */
        if (peek().kind == TOK_COLONCOLON) {
            eat(TOK_COLONCOLON);
            Token variant = eat(TOK_IDENT);

            Node *n = node_new(NODE_EXPR_ENUM_INIT);
            strcpy(n->enum_init.ename, name.text);
            strcpy(n->enum_init.vname, variant.text);
            n->enum_init.nargs = 0;

            if (peek().kind == TOK_LPAREN) {
                eat(TOK_LPAREN);
                while (peek().kind != TOK_RPAREN) {
                    n->enum_init.args[n->enum_init.nargs++] = parse_expr();
                    if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
                }
                eat(TOK_RPAREN);
            }
            return n;
        }

        /* function call: name(...) - but not print */
        if (peek().kind == TOK_LPAREN && strcmp(name.text, "print") != 0) {
            eat(TOK_LPAREN);
            Node *n = node_new(NODE_EXPR_CALL);
            strcpy(n->call.name, name.text);
            n->call.nargs = 0;
            while (peek().kind != TOK_RPAREN) {
                n->call.args[n->call.nargs++] = parse_expr();
                if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
            }
            eat(TOK_RPAREN);
            return n;
        }

        Node *n = node_new(NODE_EXPR_IDENT);
        strcpy(n->ident.name, name.text);
        return n;
    }

    fprintf(stderr, "moxy: unexpected '%s' in expression at %d:%d\n",
            t.text, t.line, t.col);
    exit(1);
}

/* ---- postfix (dot, index, method) ---- */

static Node *parse_postfix(void) {
    Node *left = parse_primary();

    for (;;) {
        if (peek().kind == TOK_DOT) {
            eat(TOK_DOT);
            Token name = eat(TOK_IDENT);

            if (peek().kind == TOK_LPAREN) {
                eat(TOK_LPAREN);
                Node *n = node_new(NODE_EXPR_METHOD);
                n->method.target = left;
                strcpy(n->method.name, name.text);
                n->method.nargs = 0;
                while (peek().kind != TOK_RPAREN) {
                    n->method.args[n->method.nargs++] = parse_expr();
                    if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
                }
                eat(TOK_RPAREN);
                left = n;
                continue;
            }

            Node *n = node_new(NODE_EXPR_FIELD);
            n->field.target = left;
            strcpy(n->field.name, name.text);
            left = n;
            continue;
        }

        if (peek().kind == TOK_LBRACKET) {
            eat(TOK_LBRACKET);
            Node *n = node_new(NODE_EXPR_INDEX);
            n->index.target = left;
            n->index.idx = parse_expr();
            eat(TOK_RBRACKET);
            left = n;
            continue;
        }

        /* postfix ++ / -- */
        if (peek().kind == TOK_PLUSPLUS || peek().kind == TOK_MINUSMINUS) {
            Token op = advance();
            Node *n = node_new(NODE_EXPR_UNARY);
            strcpy(n->unary.op, op.kind == TOK_PLUSPLUS ? "p++" : "p--");
            n->unary.operand = left;
            left = n;
            continue;
        }

        break;
    }

    return left;
}

/* ---- precedence climbing for binary ops ---- */

static Node *parse_expr_prec(int min_prec) {
    Node *left = parse_postfix();

    for (;;) {
        int prec = binop_prec(peek().kind);
        if (prec < min_prec) break;

        Token op = advance();
        Node *right = parse_expr_prec(prec + 1);

        Node *n = node_new(NODE_EXPR_BINOP);
        strcpy(n->binop.op, binop_str(op.kind));
        n->binop.left = left;
        n->binop.right = right;
        left = n;
    }

    return left;
}

static Node *parse_expr(void) {
    return parse_expr_prec(1);
}

/* ---- statements ---- */

static Node *parse_print(void) {
    eat(TOK_IDENT);
    eat(TOK_LPAREN);
    Node *n = node_new(NODE_PRINT_STMT);
    n->print_stmt.arg = parse_expr();
    eat(TOK_RPAREN);
    if (peek().kind == TOK_SEMI) eat(TOK_SEMI);
    return n;
}

static Node *parse_match(void) {
    eat(TOK_MATCH_KW);
    Token target = eat(TOK_IDENT);
    eat(TOK_LBRACE);

    Node *n = node_new(NODE_MATCH_STMT);
    strcpy(n->match_stmt.target, target.text);
    n->match_stmt.narms = 0;

    while (peek().kind != TOK_RBRACE) {
        MatchArm *arm = &n->match_stmt.arms[n->match_stmt.narms++];
        arm->pattern.binding[0] = '\0';

        if (peek().kind == TOK_OK_KW || peek().kind == TOK_ERR_KW) {
            Token kw = advance();
            arm->pattern.enum_name[0] = '\0';
            strcpy(arm->pattern.variant, kw.text);

            if (peek().kind == TOK_LPAREN) {
                eat(TOK_LPAREN);
                Token binding = eat(TOK_IDENT);
                strcpy(arm->pattern.binding, binding.text);
                eat(TOK_RPAREN);
            }
        } else {
            Token ename = eat(TOK_IDENT);
            eat(TOK_COLONCOLON);
            Token vname = eat(TOK_IDENT);

            strcpy(arm->pattern.enum_name, ename.text);
            strcpy(arm->pattern.variant, vname.text);

            if (peek().kind == TOK_LPAREN) {
                eat(TOK_LPAREN);
                Token binding = eat(TOK_IDENT);
                strcpy(arm->pattern.binding, binding.text);
                eat(TOK_RPAREN);
            }
        }

        eat(TOK_FATARROW);
        arm->body = parse_stmt();

        if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
    }

    eat(TOK_RBRACE);
    return n;
}

static Node *parse_if_stmt(void) {
    eat(TOK_IF_KW);
    eat(TOK_LPAREN);
    Node *n = node_new(NODE_IF_STMT);
    n->if_stmt.cond = parse_expr();
    eat(TOK_RPAREN);

    /* then body - stored in then_body as array (reuse decls pattern) */
    /* We need an array. Let's store then_body as Node* pointing to a block */
    Node *then_block = node_new(NODE_BLOCK);
    eat(TOK_LBRACE);
    then_block->block.nstmts = 0;
    while (peek().kind != TOK_RBRACE)
        then_block->block.stmts[then_block->block.nstmts++] = parse_stmt();
    eat(TOK_RBRACE);
    n->if_stmt.then_body = then_block;
    n->if_stmt.nthen = then_block->block.nstmts;

    n->if_stmt.else_body = NULL;
    n->if_stmt.nelse = 0;

    if (peek().kind == TOK_ELSE_KW) {
        eat(TOK_ELSE_KW);
        if (peek().kind == TOK_IF_KW) {
            /* else if - wrap in block with single if stmt */
            Node *else_block = node_new(NODE_BLOCK);
            else_block->block.nstmts = 1;
            else_block->block.stmts[0] = parse_if_stmt();
            n->if_stmt.else_body = else_block;
            n->if_stmt.nelse = 1;
        } else {
            Node *else_block = node_new(NODE_BLOCK);
            eat(TOK_LBRACE);
            else_block->block.nstmts = 0;
            while (peek().kind != TOK_RBRACE)
                else_block->block.stmts[else_block->block.nstmts++] = parse_stmt();
            eat(TOK_RBRACE);
            n->if_stmt.else_body = else_block;
            n->if_stmt.nelse = else_block->block.nstmts;
        }
    }

    return n;
}

static Node *parse_while_stmt(void) {
    eat(TOK_WHILE_KW);
    eat(TOK_LPAREN);
    Node *n = node_new(NODE_WHILE_STMT);
    n->while_stmt.cond = parse_expr();
    eat(TOK_RPAREN);
    eat(TOK_LBRACE);
    n->while_stmt.nbody = 0;
    while (peek().kind != TOK_RBRACE)
        n->while_stmt.body[n->while_stmt.nbody++] = parse_stmt();
    eat(TOK_RBRACE);
    return n;
}

static Node *parse_for_stmt(void) {
    eat(TOK_FOR_KW);
    eat(TOK_LPAREN);
    Node *n = node_new(NODE_FOR_STMT);

    /* init: var decl or expr stmt */
    if (is_type_start(peek())) {
        int save = pos;
        char type[64];
        parse_type(type);
        if (peek().kind == TOK_IDENT) {
            Token name = eat(TOK_IDENT);
            eat(TOK_EQ);
            Node *vd = node_new(NODE_VAR_DECL);
            strcpy(vd->var_decl.type, type);
            strcpy(vd->var_decl.name, name.text);
            vd->var_decl.value = parse_expr();
            n->for_stmt.init = vd;
        } else {
            pos = save;
            n->for_stmt.init = parse_expr();
        }
    } else {
        n->for_stmt.init = parse_expr();
    }
    eat(TOK_SEMI);

    /* condition */
    n->for_stmt.cond = parse_expr();
    eat(TOK_SEMI);

    /* step: could be assignment (x = x + 1) or expr (x++) */
    Node *step_expr = parse_expr();
    if (peek().kind == TOK_EQ || peek().kind == TOK_PLUSEQ ||
        peek().kind == TOK_MINUSEQ || peek().kind == TOK_STAREQ ||
        peek().kind == TOK_SLASHEQ) {
        Token op = advance();
        Node *a = node_new(NODE_ASSIGN);
        a->assign.target = step_expr;
        if (op.kind == TOK_EQ) strcpy(a->assign.op, "=");
        else if (op.kind == TOK_PLUSEQ) strcpy(a->assign.op, "+=");
        else if (op.kind == TOK_MINUSEQ) strcpy(a->assign.op, "-=");
        else if (op.kind == TOK_STAREQ) strcpy(a->assign.op, "*=");
        else if (op.kind == TOK_SLASHEQ) strcpy(a->assign.op, "/=");
        a->assign.value = parse_expr();
        n->for_stmt.step = a;
    } else {
        Node *es = node_new(NODE_EXPR_STMT);
        es->expr_stmt.expr = step_expr;
        n->for_stmt.step = es;
    }

    eat(TOK_RPAREN);
    eat(TOK_LBRACE);
    n->for_stmt.nbody = 0;
    while (peek().kind != TOK_RBRACE)
        n->for_stmt.body[n->for_stmt.nbody++] = parse_stmt();
    eat(TOK_RBRACE);
    return n;
}

static Node *parse_return_stmt(void) {
    eat(TOK_RETURN_KW);
    Node *n = node_new(NODE_RETURN_STMT);
    if (peek().kind != TOK_SEMI)
        n->return_stmt.value = parse_expr();
    else
        n->return_stmt.value = NULL;
    eat(TOK_SEMI);
    return n;
}

static Node *parse_stmt(void) {
    Token t = peek();

    if (t.kind == TOK_IDENT && strcmp(t.text, "print") == 0)
        return parse_print();

    if (t.kind == TOK_MATCH_KW)
        return parse_match();

    if (t.kind == TOK_IF_KW)
        return parse_if_stmt();

    if (t.kind == TOK_WHILE_KW)
        return parse_while_stmt();

    if (t.kind == TOK_FOR_KW)
        return parse_for_stmt();

    if (t.kind == TOK_RETURN_KW)
        return parse_return_stmt();

    /* var decl: type name = expr; */
    if (is_type_start(t)) {
        int save = pos;
        char type[64];
        parse_type(type);

        if (peek().kind == TOK_IDENT) {
            Token name = eat(TOK_IDENT);
            if (peek().kind == TOK_EQ) {
                eat(TOK_EQ);
                Node *n = node_new(NODE_VAR_DECL);
                strcpy(n->var_decl.type, type);
                strcpy(n->var_decl.name, name.text);
                n->var_decl.value = parse_expr();
                eat(TOK_SEMI);
                return n;
            }
            /* might be a function call that starts with a type-like ident */
            pos = save;
        } else {
            pos = save;
        }
    }

    /* expression, possibly followed by assignment */
    {
        Node *expr = parse_expr();

        /* assignment: expr = expr; or expr += expr; etc. */
        if (peek().kind == TOK_EQ || peek().kind == TOK_PLUSEQ ||
            peek().kind == TOK_MINUSEQ || peek().kind == TOK_STAREQ ||
            peek().kind == TOK_SLASHEQ) {
            Token op = advance();
            Node *n = node_new(NODE_ASSIGN);
            n->assign.target = expr;
            if (op.kind == TOK_EQ) strcpy(n->assign.op, "=");
            else if (op.kind == TOK_PLUSEQ) strcpy(n->assign.op, "+=");
            else if (op.kind == TOK_MINUSEQ) strcpy(n->assign.op, "-=");
            else if (op.kind == TOK_STAREQ) strcpy(n->assign.op, "*=");
            else if (op.kind == TOK_SLASHEQ) strcpy(n->assign.op, "/=");
            n->assign.value = parse_expr();
            eat(TOK_SEMI);
            return n;
        }

        Node *n = node_new(NODE_EXPR_STMT);
        n->expr_stmt.expr = expr;
        if (peek().kind == TOK_SEMI) eat(TOK_SEMI);
        return n;
    }
}

static Node *parse_enum(void) {
    eat(TOK_ENUM_KW);
    Token name = eat(TOK_IDENT);
    eat(TOK_LBRACE);

    Node *n = node_new(NODE_ENUM_DECL);
    strcpy(n->enum_decl.name, name.text);
    n->enum_decl.nvariants = 0;

    while (peek().kind != TOK_RBRACE) {
        Variant *v = &n->enum_decl.variants[n->enum_decl.nvariants++];
        Token vname = eat(TOK_IDENT);
        strcpy(v->name, vname.text);
        v->nfields = 0;

        if (peek().kind == TOK_LPAREN) {
            eat(TOK_LPAREN);
            while (peek().kind != TOK_RPAREN) {
                Field *f = &v->fields[v->nfields++];
                Token ftype = advance();
                Token fname = eat(TOK_IDENT);
                strcpy(f->type, ftype.text);
                strcpy(f->name, fname.text);
                if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
            }
            eat(TOK_RPAREN);
        }

        if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
    }

    eat(TOK_RBRACE);
    return n;
}

static Node *parse_func(const char *ret, const char *fname) {
    eat(TOK_LPAREN);

    Node *n = node_new(NODE_FUNC_DECL);
    strcpy(n->func_decl.ret, ret);
    strcpy(n->func_decl.name, fname);
    n->func_decl.nparams = 0;
    n->func_decl.nbody = 0;

    /* parse parameters */
    while (peek().kind != TOK_RPAREN) {
        Param *p = &n->func_decl.params[n->func_decl.nparams++];
        char ptype[64];
        parse_type(ptype);
        strcpy(p->type, ptype);
        Token pname = eat(TOK_IDENT);
        strcpy(p->name, pname.text);
        if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
    }

    eat(TOK_RPAREN);
    eat(TOK_LBRACE);

    while (peek().kind != TOK_RBRACE) {
        n->func_decl.body[n->func_decl.nbody++] = parse_stmt();
    }

    eat(TOK_RBRACE);
    return n;
}

Node *parse(Token *tokens, int ntokens) {
    toks = tokens;
    pos = 0;
    (void)ntokens;

    Node *prog = node_new(NODE_PROGRAM);
    prog->program.ndecls = 0;

    while (peek().kind != TOK_EOF) {
        if (peek().kind == TOK_ENUM_KW) {
            prog->program.decls[prog->program.ndecls++] = parse_enum();
            continue;
        }

        if (is_type_start(peek())) {
            int save = pos;
            char type[64];
            parse_type(type);
            Token name_tok = eat(TOK_IDENT);

            if (peek().kind == TOK_LPAREN) {
                prog->program.decls[prog->program.ndecls++] =
                    parse_func(type, name_tok.text);
            } else if (peek().kind == TOK_EQ) {
                eat(TOK_EQ);
                Node *n = node_new(NODE_VAR_DECL);
                strcpy(n->var_decl.type, type);
                strcpy(n->var_decl.name, name_tok.text);
                n->var_decl.value = parse_expr();
                eat(TOK_SEMI);
                prog->program.decls[prog->program.ndecls++] = n;
            } else {
                pos = save;
                fprintf(stderr, "moxy: unexpected '%s' at top level %d:%d\n",
                        peek().text, peek().line, peek().col);
                exit(1);
            }
            continue;
        }

        fprintf(stderr, "moxy: unexpected '%s' at top level %d:%d\n",
                peek().text, peek().line, peek().col);
        exit(1);
    }

    return prog;
}
