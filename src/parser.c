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

/* ---- type detection ---- */

static int is_type_start(Token t) {
    return t.kind == TOK_STRING_KW || t.kind == TOK_INT_KW ||
           t.kind == TOK_FLOAT_KW || t.kind == TOK_DOUBLE_KW ||
           t.kind == TOK_CHAR_KW || t.kind == TOK_BOOL_KW ||
           t.kind == TOK_LONG_KW || t.kind == TOK_SHORT_KW ||
           t.kind == TOK_VOID_KW || t.kind == TOK_RESULT_KW ||
           t.kind == TOK_MAP_KW || t.kind == TOK_IDENT ||
           t.kind == TOK_STRUCT_KW || t.kind == TOK_UNION_KW ||
           t.kind == TOK_UNSIGNED_KW || t.kind == TOK_SIGNED_KW ||
           t.kind == TOK_CONST_KW || t.kind == TOK_STATIC_KW ||
           t.kind == TOK_EXTERN_KW || t.kind == TOK_VOLATILE_KW ||
           t.kind == TOK_REGISTER_KW || t.kind == TOK_INLINE_KW ||
           t.kind == TOK_ENUM_KW;
}

/* ---- type parsing ---- */

static void parse_type(char *buf) {
    buf[0] = '\0';

    /* leading qualifiers */
    for (;;) {
        Token t = peek();
        if (t.kind == TOK_CONST_KW || t.kind == TOK_VOLATILE_KW ||
            t.kind == TOK_STATIC_KW || t.kind == TOK_EXTERN_KW ||
            t.kind == TOK_REGISTER_KW || t.kind == TOK_INLINE_KW) {
            if (buf[0]) strcat(buf, " ");
            strcat(buf, t.text);
            advance();
        } else {
            break;
        }
    }

    Token t = peek();

    /* Result<T> */
    if (t.kind == TOK_RESULT_KW) {
        advance();
        eat(TOK_LT);
        char inner[64];
        parse_type(inner);
        eat(TOK_GT);
        char tmp[64];
        snprintf(tmp, 64, "Result<%s>", inner);
        if (buf[0]) strcat(buf, " ");
        strcat(buf, tmp);
        return;
    }

    /* map[K,V] */
    if (t.kind == TOK_MAP_KW) {
        advance();
        eat(TOK_LBRACKET);
        char key[64];
        parse_type(key);
        eat(TOK_COMMA);
        char val[64];
        parse_type(val);
        eat(TOK_RBRACKET);
        char tmp[64];
        snprintf(tmp, 64, "map[%s,%s]", key, val);
        if (buf[0]) strcat(buf, " ");
        strcat(buf, tmp);
        return;
    }

    /* struct/union/enum Name */
    if (t.kind == TOK_STRUCT_KW || t.kind == TOK_UNION_KW || t.kind == TOK_ENUM_KW) {
        advance();
        if (buf[0]) strcat(buf, " ");
        strcat(buf, t.text);
        if (peek().kind == TOK_IDENT) {
            Token name = advance();
            strcat(buf, " ");
            strcat(buf, name.text);
        }
        /* pointer suffixes */
        while (peek().kind == TOK_STAR) {
            advance();
            strcat(buf, "*");
        }
        return;
    }

    /* multi-word: unsigned int, unsigned long, signed char, long long, long double */
    if (t.kind == TOK_UNSIGNED_KW || t.kind == TOK_SIGNED_KW) {
        advance();
        if (buf[0]) strcat(buf, " ");
        strcat(buf, t.text);
        Token next = peek();
        if (next.kind == TOK_INT_KW || next.kind == TOK_LONG_KW ||
            next.kind == TOK_SHORT_KW || next.kind == TOK_CHAR_KW) {
            advance();
            strcat(buf, " ");
            strcat(buf, next.text);
            /* unsigned long long */
            if ((next.kind == TOK_LONG_KW) && peek().kind == TOK_LONG_KW) {
                Token ll = advance();
                strcat(buf, " ");
                strcat(buf, ll.text);
            }
        }
        /* pointer suffixes */
        while (peek().kind == TOK_STAR) {
            advance();
            strcat(buf, "*");
        }
        return;
    }

    if (t.kind == TOK_LONG_KW) {
        advance();
        if (buf[0]) strcat(buf, " ");
        strcat(buf, t.text);
        /* long long, long double, long int */
        Token next = peek();
        if (next.kind == TOK_LONG_KW || next.kind == TOK_DOUBLE_KW ||
            next.kind == TOK_INT_KW) {
            advance();
            strcat(buf, " ");
            strcat(buf, next.text);
        }
        /* pointer suffixes */
        while (peek().kind == TOK_STAR) {
            advance();
            strcat(buf, "*");
        }
        return;
    }

    /* simple base type or identifier */
    advance();
    if (buf[0]) strcat(buf, " ");
    strcat(buf, t.text);

    /* T[] list shorthand */
    if (peek().kind == TOK_LBRACKET &&
        toks[pos + 1].kind == TOK_RBRACKET) {
        eat(TOK_LBRACKET);
        eat(TOK_RBRACKET);
        char base[64];
        strcpy(base, buf);
        snprintf(buf, 64, "%s[]", base);
        return;
    }

    /* pointer suffixes */
    while (peek().kind == TOK_STAR) {
        advance();
        strcat(buf, "*");
    }
}

static Node *parse_expr(void);
static Node *parse_expr_prec(int min_prec);
static Node *parse_stmt(void);

/* ---- raw passthrough helpers ---- */

static int no_space_after(TokenKind k) {
    return k == TOK_LPAREN || k == TOK_LBRACKET || k == TOK_LBRACE ||
           k == TOK_DOT || k == TOK_ARROW || k == TOK_TILDE ||
           k == TOK_BANG || k == TOK_AMP || k == TOK_STAR;
}

static int no_space_before(TokenKind k) {
    return k == TOK_RPAREN || k == TOK_RBRACKET || k == TOK_RBRACE ||
           k == TOK_DOT || k == TOK_COMMA || k == TOK_SEMI ||
           k == TOK_ARROW || k == TOK_PLUSPLUS || k == TOK_MINUSMINUS ||
           k == TOK_COLON || k == TOK_LBRACKET;
}

static Node *raw_from_range(int start, int end) {
    /* estimate size */
    int sz = 0;
    for (int i = start; i < end; i++)
        sz += (int)strlen(toks[i].text) + 2;
    sz += 1;

    char *buf = malloc(sz);
    int bpos = 0;
    for (int i = start; i < end; i++) {
        if (i > start) {
            int prev_kind = toks[i-1].kind;
            int cur_kind = toks[i].kind;
            if (!no_space_after(prev_kind) && !no_space_before(cur_kind)) {
                buf[bpos++] = ' ';
            }
        }
        int tlen = (int)strlen(toks[i].text);
        /* wrap string literals in quotes */
        if (toks[i].kind == TOK_STRLIT) {
            buf[bpos++] = '"';
            memcpy(buf + bpos, toks[i].text, tlen);
            bpos += tlen;
            buf[bpos++] = '"';
        } else if (toks[i].kind == TOK_CHARLIT) {
            buf[bpos++] = '\'';
            memcpy(buf + bpos, toks[i].text, tlen);
            bpos += tlen;
            buf[bpos++] = '\'';
        } else {
            memcpy(buf + bpos, toks[i].text, tlen);
            bpos += tlen;
        }
    }
    buf[bpos] = '\0';

    Node *n = node_new(NODE_RAW);
    n->raw.text = buf;
    return n;
}

static Node *collect_raw_toplevel(void) {
    int start = pos;
    int depth = 0;
    int saw_brace = 0;

    while (toks[pos].kind != TOK_EOF) {
        TokenKind k = toks[pos].kind;
        if (k == TOK_LBRACE || k == TOK_LPAREN || k == TOK_LBRACKET) {
            if (k == TOK_LBRACE) saw_brace = 1;
            depth++;
        }
        if (k == TOK_RBRACE || k == TOK_RPAREN || k == TOK_RBRACKET) depth--;

        if (k == TOK_SEMI && depth == 0) {
            pos++; /* consume ; */
            return raw_from_range(start, pos);
        }
        /* } at depth 0: stop if no ; follows soon (function-like body)
           but if ; comes eventually at depth 0, let it be caught above */
        if (k == TOK_RBRACE && depth == 0 && saw_brace) {
            pos++; /* consume } */
            /* check if more tokens follow on this declaration (e.g. typedef ... } Name;) */
            if (toks[pos].kind == TOK_SEMI) {
                pos++; /* consume ; */
                return raw_from_range(start, pos);
            }
            /* if next token is an identifier or *, continue (typedef struct {...} Name;) */
            if (toks[pos].kind == TOK_IDENT || toks[pos].kind == TOK_STAR) {
                continue; /* keep scanning for ; */
            }
            /* otherwise this } ends the declaration */
            return raw_from_range(start, pos);
        }
        pos++;
    }

    return raw_from_range(start, pos);
}

static Node *collect_raw_stmt(void) {
    int start = pos;
    int depth = 0;

    while (toks[pos].kind != TOK_EOF) {
        TokenKind k = toks[pos].kind;
        if (k == TOK_LBRACE || k == TOK_LPAREN || k == TOK_LBRACKET) depth++;
        if (k == TOK_RBRACE || k == TOK_RPAREN || k == TOK_RBRACKET) depth--;

        /* if depth goes negative, we hit the enclosing }, don't consume it */
        if (depth < 0) break;

        if (k == TOK_SEMI && depth == 0) {
            pos++; /* consume ; */
            return raw_from_range(start, pos);
        }
        if (k == TOK_RBRACE && depth == 0) {
            pos++; /* consume } */
            /* do-while: } followed by while(...); — keep scanning */
            if (toks[pos].kind == TOK_WHILE_KW) {
                continue; /* keep scanning for the ; */
            }
            /* eat trailing ; after other block constructs */
            if (toks[pos].kind == TOK_SEMI) pos++;
            return raw_from_range(start, pos);
        }
        pos++;
    }

    return raw_from_range(start, pos);
}

/* ---- expression detection ---- */

static int is_expr_start(Token t) {
    return t.kind == TOK_IDENT || t.kind == TOK_LPAREN ||
           t.kind == TOK_INTLIT || t.kind == TOK_FLOATLIT ||
           t.kind == TOK_STRLIT || t.kind == TOK_CHARLIT ||
           t.kind == TOK_TRUE_KW || t.kind == TOK_FALSE_KW ||
           t.kind == TOK_NULL_KW || t.kind == TOK_OK_KW ||
           t.kind == TOK_ERR_KW || t.kind == TOK_BANG ||
           t.kind == TOK_MINUS || t.kind == TOK_LBRACKET ||
           t.kind == TOK_LBRACE || t.kind == TOK_STAR ||
           t.kind == TOK_AMP || t.kind == TOK_PLUSPLUS ||
           t.kind == TOK_MINUSMINUS || t.kind == TOK_SIZEOF_KW ||
           t.kind == TOK_TILDE;
}

/* ---- C type keyword check (for cast detection) ---- */

static int is_c_type_keyword(Token t) {
    return t.kind == TOK_INT_KW || t.kind == TOK_CHAR_KW ||
           t.kind == TOK_FLOAT_KW || t.kind == TOK_DOUBLE_KW ||
           t.kind == TOK_VOID_KW || t.kind == TOK_LONG_KW ||
           t.kind == TOK_SHORT_KW || t.kind == TOK_BOOL_KW ||
           t.kind == TOK_STRING_KW ||
           t.kind == TOK_STRUCT_KW || t.kind == TOK_UNION_KW ||
           t.kind == TOK_UNSIGNED_KW || t.kind == TOK_SIGNED_KW ||
           t.kind == TOK_CONST_KW || t.kind == TOK_VOLATILE_KW ||
           t.kind == TOK_ENUM_KW;
}

/* ---- operator precedence ---- */

static int binop_prec(TokenKind k) {
    switch (k) {
    case TOK_OR:      return 1;
    case TOK_AND:     return 2;
    case TOK_PIPE:    return 3;
    case TOK_CARET:   return 4;
    case TOK_AMP:     return 5;
    case TOK_EQEQ:
    case TOK_NEQ:     return 6;
    case TOK_LT:
    case TOK_GT:
    case TOK_LTEQ:
    case TOK_GTEQ:    return 7;
    case TOK_LSHIFT:
    case TOK_RSHIFT:  return 8;
    case TOK_PLUS:
    case TOK_MINUS:   return 9;
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT: return 10;
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
    case TOK_PIPE:    return "|";
    case TOK_CARET:   return "^";
    case TOK_AMP:     return "&";
    case TOK_LSHIFT:  return "<<";
    case TOK_RSHIFT:  return ">>";
    default:          return "?";
    }
}

/* ---- primary expressions ---- */

static Node *parse_primary(void) {
    Token t = peek();

    /* cast expression: (type)expr */
    if (t.kind == TOK_LPAREN) {
        /* check if this looks like a cast: ( type_keyword ... ) */
        if (is_c_type_keyword(toks[pos + 1])) {
            int save = pos;
            advance(); /* eat ( */
            /* collect type tokens until ) */
            int tstart = pos;
            int depth = 1;
            while (toks[pos].kind != TOK_EOF && depth > 0) {
                if (toks[pos].kind == TOK_LPAREN) depth++;
                if (toks[pos].kind == TOK_RPAREN) depth--;
                if (depth > 0) pos++;
            }
            int tend = pos;
            if (toks[pos].kind == TOK_RPAREN) pos++; /* eat ) */

            /* check if what follows looks like an expression (otherwise it might be a paren expr) */
            if (is_expr_start(peek()) || peek().kind == TOK_LPAREN) {
                /* build type text */
                char tbuf[128] = {0};
                for (int i = tstart; i < tend; i++) {
                    if (i > tstart) strcat(tbuf, " ");
                    if (toks[i].kind == TOK_STAR) {
                        /* remove trailing space before * */
                        int len = (int)strlen(tbuf);
                        if (len > 0 && tbuf[len-1] == ' ') tbuf[len-1] = '\0';
                        strcat(tbuf, "*");
                    } else {
                        strcat(tbuf, toks[i].text);
                    }
                }
                Node *n = node_new(NODE_EXPR_CAST);
                strcpy(n->cast.type_text, tbuf);
                n->cast.operand = parse_primary();
                return n;
            }

            /* not a cast, restore and treat as paren expr */
            pos = save;
        }

        /* regular paren expression */
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
        n->intlit.value = (int)strtol(t.text, NULL, 0);
        strcpy(n->intlit.text, t.text);
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

    /* brace initializer: {expr, expr, ...} or {} */
    if (t.kind == TOK_LBRACE) {
        int start = pos;
        advance(); /* eat { */
        if (peek().kind == TOK_RBRACE) {
            advance(); /* eat } */
            return node_new(NODE_EXPR_EMPTY);
        }
        /* collect all tokens inside braces as raw */
        int depth = 1;
        while (toks[pos].kind != TOK_EOF && depth > 0) {
            if (toks[pos].kind == TOK_LBRACE) depth++;
            if (toks[pos].kind == TOK_RBRACE) depth--;
            if (depth > 0) pos++;
        }
        if (toks[pos].kind == TOK_RBRACE) pos++; /* eat closing } */
        return raw_from_range(start, pos);
    }

    /* sizeof(type/expr) */
    if (t.kind == TOK_SIZEOF_KW) {
        int start = pos;
        advance(); /* eat sizeof */
        if (peek().kind == TOK_LPAREN) {
            advance(); /* eat ( */
            int depth = 1;
            while (toks[pos].kind != TOK_EOF && depth > 0) {
                if (toks[pos].kind == TOK_LPAREN) depth++;
                if (toks[pos].kind == TOK_RPAREN) depth--;
                if (depth > 0) pos++;
            }
            if (toks[pos].kind == TOK_RPAREN) pos++; /* eat ) */
        }
        return raw_from_range(start, pos);
    }

    /* unary operators: !, -, ~, &(address-of), *(deref), ++, -- */
    if (t.kind == TOK_BANG || t.kind == TOK_MINUS || t.kind == TOK_TILDE) {
        advance();
        Node *n = node_new(NODE_EXPR_UNARY);
        strcpy(n->unary.op, t.text);
        n->unary.operand = parse_primary();
        return n;
    }

    if (t.kind == TOK_AMP) {
        advance();
        Node *n = node_new(NODE_EXPR_UNARY);
        strcpy(n->unary.op, "&");
        n->unary.operand = parse_primary();
        return n;
    }

    if (t.kind == TOK_STAR) {
        advance();
        Node *n = node_new(NODE_EXPR_UNARY);
        strcpy(n->unary.op, "*");
        n->unary.operand = parse_primary();
        return n;
    }

    if (t.kind == TOK_PLUSPLUS) {
        advance();
        Node *n = node_new(NODE_EXPR_UNARY);
        strcpy(n->unary.op, "++");
        n->unary.operand = parse_primary();
        return n;
    }

    if (t.kind == TOK_MINUSMINUS) {
        advance();
        Node *n = node_new(NODE_EXPR_UNARY);
        strcpy(n->unary.op, "--");
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

/* ---- postfix (dot, arrow, index, method) ---- */

static Node *parse_postfix(void) {
    Node *left = parse_primary();

    for (;;) {
        /* dot access */
        if (peek().kind == TOK_DOT) {
            eat(TOK_DOT);
            Token name = eat(TOK_IDENT);

            if (peek().kind == TOK_LPAREN) {
                eat(TOK_LPAREN);
                Node *n = node_new(NODE_EXPR_METHOD);
                n->method.target = left;
                strcpy(n->method.name, name.text);
                n->method.nargs = 0;
                n->method.is_arrow = 0;
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
            n->field.is_arrow = 0;
            left = n;
            continue;
        }

        /* arrow access -> */
        if (peek().kind == TOK_ARROW) {
            eat(TOK_ARROW);
            Token name = eat(TOK_IDENT);

            if (peek().kind == TOK_LPAREN) {
                eat(TOK_LPAREN);
                Node *n = node_new(NODE_EXPR_METHOD);
                n->method.target = left;
                strcpy(n->method.name, name.text);
                n->method.nargs = 0;
                n->method.is_arrow = 1;
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
            n->field.is_arrow = 1;
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

/* ---- precedence climbing for binary ops + ternary ---- */

static Node *parse_expr_prec(int min_prec) {
    Node *left = parse_postfix();

    for (;;) {
        int prec = binop_prec(peek().kind);
        if (prec < min_prec) break;

        /* disambiguate: TOK_AMP at prec 5 could be bitwise-and, but
           if we're in a context where it'd conflict with address-of,
           we've already handled prefix in parse_primary. Here it's always binary. */

        Token op = advance();
        Node *right = parse_expr_prec(prec + 1);

        Node *n = node_new(NODE_EXPR_BINOP);
        strcpy(n->binop.op, binop_str(op.kind));
        n->binop.left = left;
        n->binop.right = right;
        left = n;
    }

    /* ternary: expr ? expr : expr */
    if (peek().kind == TOK_QUESTION) {
        advance(); /* eat ? */
        Node *then_expr = parse_expr();
        eat(TOK_COLON);
        Node *else_expr = parse_expr_prec(1);

        Node *n = node_new(NODE_EXPR_TERNARY);
        n->ternary.cond = left;
        n->ternary.then_expr = then_expr;
        n->ternary.else_expr = else_expr;
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

static int is_assign_op(TokenKind k) {
    return k == TOK_EQ || k == TOK_PLUSEQ || k == TOK_MINUSEQ ||
           k == TOK_STAREQ || k == TOK_SLASHEQ ||
           k == TOK_AMPEQ || k == TOK_PIPEEQ || k == TOK_CARETEQ ||
           k == TOK_PERCENTEQ || k == TOK_LSHIFTEQ || k == TOK_RSHIFTEQ;
}

static void assign_op_str(TokenKind k, char *buf) {
    switch (k) {
    case TOK_EQ:        strcpy(buf, "="); break;
    case TOK_PLUSEQ:    strcpy(buf, "+="); break;
    case TOK_MINUSEQ:   strcpy(buf, "-="); break;
    case TOK_STAREQ:    strcpy(buf, "*="); break;
    case TOK_SLASHEQ:   strcpy(buf, "/="); break;
    case TOK_AMPEQ:     strcpy(buf, "&="); break;
    case TOK_PIPEEQ:    strcpy(buf, "|="); break;
    case TOK_CARETEQ:   strcpy(buf, "^="); break;
    case TOK_PERCENTEQ: strcpy(buf, "%="); break;
    case TOK_LSHIFTEQ:  strcpy(buf, "<<="); break;
    case TOK_RSHIFTEQ:  strcpy(buf, ">>="); break;
    default:            strcpy(buf, "="); break;
    }
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
    if (is_assign_op(peek().kind)) {
        Token op = advance();
        Node *a = node_new(NODE_ASSIGN);
        a->assign.target = step_expr;
        assign_op_str(op.kind, a->assign.op);
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

    /* label: ident followed by colon (not ::) */
    if (t.kind == TOK_IDENT && toks[pos + 1].kind == TOK_COLON)
        return collect_raw_stmt();

    /* C statement keywords that we pass through raw */
    if (t.kind == TOK_SWITCH_KW || t.kind == TOK_DO_KW ||
        t.kind == TOK_GOTO_KW || t.kind == TOK_TYPEDEF_KW ||
        t.kind == TOK_STRUCT_KW || t.kind == TOK_UNION_KW ||
        t.kind == TOK_EXTERN_KW || t.kind == TOK_REGISTER_KW ||
        t.kind == TOK_VOLATILE_KW || t.kind == TOK_INLINE_KW)
        return collect_raw_stmt();

    /* break; and continue; as raw */
    if (t.kind == TOK_BREAK_KW || t.kind == TOK_CONTINUE_KW ||
        t.kind == TOK_CASE_KW || t.kind == TOK_DEFAULT_KW)
        return collect_raw_stmt();

    /* var decl: type name = expr; or type name; */
    if (is_type_start(t)) {
        int save = pos;
        char type[64];
        parse_type(type);

        if (peek().kind == TOK_IDENT) {
            /* check the token after the ident to decide */
            Token name_tok = toks[pos];
            TokenKind after = toks[pos + 1].kind;

            if (after == TOK_EQ) {
                eat(TOK_IDENT);
                eat(TOK_EQ);
                Node *n = node_new(NODE_VAR_DECL);
                strcpy(n->var_decl.type, type);
                strcpy(n->var_decl.name, name_tok.text);
                n->var_decl.value = parse_expr();
                eat(TOK_SEMI);
                return n;
            }

            if (after == TOK_SEMI) {
                /* uninitialized var decl: int x; — pass through as raw */
                pos = save;
                return collect_raw_stmt();
            }

            if (after == TOK_LBRACKET || after == TOK_COMMA) {
                /* C array decl: int arr[10]; or int a, b; — raw */
                pos = save;
                return collect_raw_stmt();
            }

            if (after == TOK_LPAREN) {
                /* could be function pointer or nested function — but in statement
                   context this is unusual, treat as expression */
                pos = save;
            } else {
                /* might be a function call that starts with a type-like ident */
                pos = save;
            }
        } else {
            pos = save;
        }
    }

    /* if token can't start an expression, collect as raw C statement */
    if (!is_expr_start(t))
        return collect_raw_stmt();

    /* expression, possibly followed by assignment */
    {
        Node *expr = parse_expr();

        if (is_assign_op(peek().kind)) {
            Token op = advance();
            Node *n = node_new(NODE_ASSIGN);
            n->assign.target = expr;
            assign_op_str(op.kind, n->assign.op);
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

/* ---- top-level parsing ---- */

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
        /* handle ... (variadic) */
        if (peek().kind == TOK_ELLIPSIS) {
            Param *p = &n->func_decl.params[n->func_decl.nparams++];
            strcpy(p->type, "...");
            strcpy(p->name, "");
            advance();
        } else {
            Param *p = &n->func_decl.params[n->func_decl.nparams++];
            char ptype[64];
            parse_type(ptype);
            strcpy(p->type, ptype);
            Token pname = eat(TOK_IDENT);
            strcpy(p->name, pname.text);
        }
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

/* check if an enum block is a C enum (just identifiers and = integer constants,
   followed by }; ) vs a Moxy tagged enum (has parenthesized fields) */
static int is_c_enum(void) {
    int save = pos;
    /* we're positioned after "enum Name {" */
    /* scan to find if there's a ( before the }, indicating Moxy variant fields */
    int depth = 1;
    while (toks[pos].kind != TOK_EOF && depth > 0) {
        if (toks[pos].kind == TOK_LBRACE) depth++;
        if (toks[pos].kind == TOK_RBRACE) depth--;
        /* Moxy enums have (type name) field declarations inside variants */
        if (toks[pos].kind == TOK_LPAREN && depth == 1) {
            pos = save;
            return 0; /* Moxy tagged enum */
        }
        if (depth > 0) pos++;
    }
    /* check if } is followed by ; (C enum) or not (Moxy enum) */
    int result = 0;
    if (toks[pos].kind == TOK_RBRACE) {
        result = (toks[pos + 1].kind == TOK_SEMI ||
                  toks[pos + 1].kind == TOK_IDENT); /* enum Foo { A, B } var; */
    }
    pos = save;
    return result;
}

Node *parse(Token *tokens, int ntokens) {
    toks = tokens;
    pos = 0;
    (void)ntokens;

    Node *prog = node_new(NODE_PROGRAM);
    prog->program.ndecls = 0;

    while (peek().kind != TOK_EOF) {
        /* enum: distinguish Moxy tagged enum vs C enum */
        if (peek().kind == TOK_ENUM_KW) {
            /* look ahead: enum Name { ... } */
            if (toks[pos + 1].kind == TOK_IDENT &&
                toks[pos + 2].kind == TOK_LBRACE) {
                int save = pos;
                pos += 3; /* skip past enum Name { */
                int c_style = is_c_enum();
                pos = save;

                if (c_style) {
                    prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                } else {
                    prog->program.decls[prog->program.ndecls++] = parse_enum();
                }
            } else {
                /* just "enum" without ident+brace, raw passthrough */
                prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
            }
            continue;
        }

        /* typedef, struct, union at top level → raw passthrough */
        if (peek().kind == TOK_TYPEDEF_KW ||
            peek().kind == TOK_EXTERN_KW) {
            prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
            continue;
        }

        /* struct/union at top level: could be type definition or variable/function */
        if (peek().kind == TOK_STRUCT_KW || peek().kind == TOK_UNION_KW) {
            /* struct Name { ... }; (type def) vs struct Name foo or struct Name func() */
            int save = pos;
            /* look ahead for struct Name { → it's a struct definition, raw */
            advance(); /* struct/union */
            if (peek().kind == TOK_IDENT) {
                advance(); /* Name */
                if (peek().kind == TOK_LBRACE) {
                    /* struct Name { ... } — could be definition or var */
                    pos = save;
                    prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                    continue;
                }
                /* struct Name ident → possibly a var decl or function */
                pos = save;
                /* fall through to type-start handling below */
            } else if (peek().kind == TOK_LBRACE) {
                /* anonymous struct { ... } */
                pos = save;
                prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                continue;
            } else {
                pos = save;
                prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                continue;
            }
        }

        if (is_type_start(peek())) {
            int save = pos;
            char type[64];
            parse_type(type);

            if (peek().kind == TOK_IDENT) {
                Token name_tok = toks[pos];
                pos++; /* consume ident */

                if (peek().kind == TOK_LPAREN) {
                    pos--; /* unconsume ident — parse_func will re-read */
                    Token nm = eat(TOK_IDENT);
                    prog->program.decls[prog->program.ndecls++] =
                        parse_func(type, nm.text);
                } else if (peek().kind == TOK_EQ) {
                    eat(TOK_EQ);
                    Node *n = node_new(NODE_VAR_DECL);
                    strcpy(n->var_decl.type, type);
                    strcpy(n->var_decl.name, name_tok.text);
                    n->var_decl.value = parse_expr();
                    eat(TOK_SEMI);
                    prog->program.decls[prog->program.ndecls++] = n;
                } else {
                    /* something else (C array, uninitialized global, etc.) → raw */
                    pos = save;
                    prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                }
            } else {
                /* type but no ident after → could be function pointer, etc → raw */
                pos = save;
                prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
            }
            continue;
        }

        /* anything else at top level → raw passthrough */
        prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
    }

    return prog;
}
