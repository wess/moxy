#include "parser.h"
#include "diag.h"
#include "flags.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Token *toks;
static int pos;

static Token peek(void) { return toks[pos]; }

static Token eat(TokenKind kind) {
    Token t = toks[pos];
    if (t.kind != kind) {
        diag_error_expected(t.line, t.col, kind, t.kind, t.text);
        diag_bail();
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
           t.kind == TOK_FUTURE_KW ||
           t.kind == TOK_MAP_KW || t.kind == TOK_IDENT ||
           t.kind == TOK_STRUCT_KW || t.kind == TOK_UNION_KW ||
           t.kind == TOK_UNSIGNED_KW || t.kind == TOK_SIGNED_KW ||
           t.kind == TOK_CONST_KW || t.kind == TOK_STATIC_KW ||
           t.kind == TOK_EXTERN_KW || t.kind == TOK_VOLATILE_KW ||
           t.kind == TOK_REGISTER_KW || t.kind == TOK_INLINE_KW ||
           t.kind == TOK_ENUM_KW;
}

static void parse_type(char *buf) {
    buf[0] = '\0';

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

    /* Future<T> */
    if (t.kind == TOK_FUTURE_KW) {
        if (!moxy_async_enabled) {
            diag_error(t.line, t.col, "Future<T> requires --enable-async flag");
            diag_hint("run with: moxy --enable-async ...");
            diag_bail();
        }
        advance();
        eat(TOK_LT);
        char inner[64];
        parse_type(inner);
        eat(TOK_GT);
        char tmp[64];
        snprintf(tmp, 64, "Future<%s>", inner);
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

    if (t.kind == TOK_STRUCT_KW || t.kind == TOK_UNION_KW || t.kind == TOK_ENUM_KW) {
        advance();
        if (buf[0]) strcat(buf, " ");
        strcat(buf, t.text);
        if (peek().kind == TOK_IDENT) {
            Token name = advance();
            strcat(buf, " ");
            strcat(buf, name.text);
        }
        while (peek().kind == TOK_STAR) {
            advance();
            strcat(buf, "*");
        }
        return;
    }

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
            if ((next.kind == TOK_LONG_KW) && peek().kind == TOK_LONG_KW) {
                Token ll = advance();
                strcat(buf, " ");
                strcat(buf, ll.text);
            }
        }
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
        Token next = peek();
        if (next.kind == TOK_LONG_KW || next.kind == TOK_DOUBLE_KW ||
            next.kind == TOK_INT_KW) {
            advance();
            strcat(buf, " ");
            strcat(buf, next.text);
        }
        while (peek().kind == TOK_STAR) {
            advance();
            strcat(buf, "*");
        }
        return;
    }

    advance();
    if (buf[0]) strcat(buf, " ");
    strcat(buf, t.text);

    if (peek().kind == TOK_LBRACKET &&
        toks[pos + 1].kind == TOK_RBRACKET) {
        eat(TOK_LBRACKET);
        eat(TOK_RBRACKET);
        char base[64];
        strcpy(base, buf);
        snprintf(buf, 64, "%s[]", base);
        return;
    }

    while (peek().kind == TOK_STAR) {
        advance();
        strcat(buf, "*");
    }
}

static Node *parse_expr(void);
static Node *parse_expr_prec(int min_prec);
static Node *parse_postfix(void);
static Node *parse_stmt(void);

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
    n->line = toks[start].line;
    n->col = toks[start].col;
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
            pos++;
            return raw_from_range(start, pos);
        }
        if (k == TOK_RBRACE && depth == 0 && saw_brace) {
            pos++;
            if (toks[pos].kind == TOK_SEMI) {
                pos++;
                return raw_from_range(start, pos);
            }
            if (toks[pos].kind == TOK_IDENT || toks[pos].kind == TOK_STAR) {
                continue;
            }
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
            pos++;
            return raw_from_range(start, pos);
        }
        if (k == TOK_RBRACE && depth == 0) {
            pos++;
            if (toks[pos].kind == TOK_WHILE_KW) {
                continue;
            }
            if (toks[pos].kind == TOK_SEMI) pos++;
            return raw_from_range(start, pos);
        }
        pos++;
    }

    return raw_from_range(start, pos);
}

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
           t.kind == TOK_TILDE || t.kind == TOK_AWAIT_KW;
}

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

static int binop_prec(TokenKind k) {
    switch (k) {
    case TOK_PIPEARROW: return 0;
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

static Node *parse_primary(void) {
    Token t = peek();

    if (t.kind == TOK_LPAREN) {
        if (is_c_type_keyword(toks[pos + 1])) {
            int save = pos;
            advance();
            int tstart = pos;
            int depth = 1;
            while (toks[pos].kind != TOK_EOF && depth > 0) {
                if (toks[pos].kind == TOK_LPAREN) depth++;
                if (toks[pos].kind == TOK_RPAREN) depth--;
                if (depth > 0) pos++;
            }
            int tend = pos;
            if (toks[pos].kind == TOK_RPAREN) pos++;

            if (is_expr_start(peek()) || peek().kind == TOK_LPAREN) {
                char tbuf[128] = {0};
                for (int i = tstart; i < tend; i++) {
                    if (i > tstart) strcat(tbuf, " ");
                    if (toks[i].kind == TOK_STAR) {
                        int len = (int)strlen(tbuf);
                        if (len > 0 && tbuf[len-1] == ' ') tbuf[len-1] = '\0';
                        strcat(tbuf, "*");
                    } else {
                        strcat(tbuf, toks[i].text);
                    }
                }
                Node *n = node_new(NODE_EXPR_CAST);
                n->line = t.line;
                n->col = t.col;
                strcpy(n->cast.type_text, tbuf);
                n->cast.operand = parse_primary();
                return n;
            }

            pos = save;
        }

        advance();
        Node *n = node_new(NODE_EXPR_PAREN);
        n->line = t.line;
        n->col = t.col;
        n->paren.inner = parse_expr();
        eat(TOK_RPAREN);
        return n;
    }

    if (t.kind == TOK_STRLIT) {
        advance();
        Node *n = node_new(NODE_EXPR_STRLIT);
        n->line = t.line;
        n->col = t.col;
        strcpy(n->strlit.value, t.text);
        return n;
    }

    if (t.kind == TOK_INTLIT) {
        advance();
        Node *n = node_new(NODE_EXPR_INTLIT);
        n->line = t.line;
        n->col = t.col;
        n->intlit.value = (int)strtol(t.text, NULL, 0);
        strcpy(n->intlit.text, t.text);
        return n;
    }

    if (t.kind == TOK_FLOATLIT) {
        advance();
        Node *n = node_new(NODE_EXPR_FLOATLIT);
        n->line = t.line;
        n->col = t.col;
        strcpy(n->floatlit.value, t.text);
        return n;
    }

    if (t.kind == TOK_CHARLIT) {
        advance();
        Node *n = node_new(NODE_EXPR_CHARLIT);
        n->line = t.line;
        n->col = t.col;
        n->charlit.value = t.text[0];
        return n;
    }

    if (t.kind == TOK_TRUE_KW || t.kind == TOK_FALSE_KW) {
        advance();
        Node *n = node_new(NODE_EXPR_BOOLLIT);
        n->line = t.line;
        n->col = t.col;
        n->boollit.value = (t.kind == TOK_TRUE_KW) ? 1 : 0;
        return n;
    }

    if (t.kind == TOK_NULL_KW) {
        advance();
        Node *n = node_new(NODE_EXPR_NULL);
        n->line = t.line;
        n->col = t.col;
        return n;
    }

    if (t.kind == TOK_OK_KW) {
        advance();
        eat(TOK_LPAREN);
        Node *n = node_new(NODE_EXPR_OK);
        n->line = t.line;
        n->col = t.col;
        n->ok_expr.inner = parse_expr();
        eat(TOK_RPAREN);
        return n;
    }

    if (t.kind == TOK_ERR_KW) {
        advance();
        eat(TOK_LPAREN);
        Node *n = node_new(NODE_EXPR_ERR);
        n->line = t.line;
        n->col = t.col;
        n->err_expr.inner = parse_expr();
        eat(TOK_RPAREN);
        return n;
    }

    if (t.kind == TOK_LBRACKET) {
        advance();
        Node *n = node_new(NODE_EXPR_LIST_LIT);
        n->line = t.line;
        n->col = t.col;
        n->list_lit.nitems = 0;
        while (peek().kind != TOK_RBRACKET) {
            n->list_lit.items[n->list_lit.nitems++] = parse_expr();
            if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
        }
        eat(TOK_RBRACKET);
        return n;
    }

    if (t.kind == TOK_LBRACE) {
        int start = pos;
        advance();
        if (peek().kind == TOK_RBRACE) {
            advance();
            Node *ne = node_new(NODE_EXPR_EMPTY);
            ne->line = t.line;
            ne->col = t.col;
            return ne;
        }
        int depth = 1;
        while (toks[pos].kind != TOK_EOF && depth > 0) {
            if (toks[pos].kind == TOK_LBRACE) depth++;
            if (toks[pos].kind == TOK_RBRACE) depth--;
            if (depth > 0) pos++;
        }
        if (toks[pos].kind == TOK_RBRACE) pos++;
        return raw_from_range(start, pos);
    }

    if (t.kind == TOK_SIZEOF_KW) {
        int start = pos;
        advance();
        if (peek().kind == TOK_LPAREN) {
            advance();
            int depth = 1;
            while (toks[pos].kind != TOK_EOF && depth > 0) {
                if (toks[pos].kind == TOK_LPAREN) depth++;
                if (toks[pos].kind == TOK_RPAREN) depth--;
                if (depth > 0) pos++;
            }
            if (toks[pos].kind == TOK_RPAREN) pos++;
        }
        return raw_from_range(start, pos);
    }

    if (t.kind == TOK_BANG || t.kind == TOK_MINUS || t.kind == TOK_TILDE ||
        t.kind == TOK_AMP || t.kind == TOK_STAR) {
        advance();
        Node *n = node_new(NODE_EXPR_UNARY);
        n->line = t.line;
        n->col = t.col;
        strcpy(n->unary.op, t.text);
        n->unary.operand = parse_primary();
        return n;
    }

    if (t.kind == TOK_PLUSPLUS || t.kind == TOK_MINUSMINUS) {
        advance();
        Node *n = node_new(NODE_EXPR_UNARY);
        n->line = t.line;
        n->col = t.col;
        strcpy(n->unary.op, t.text);
        n->unary.operand = parse_primary();
        return n;
    }

    if (t.kind == TOK_AWAIT_KW) {
        if (!moxy_async_enabled) {
            diag_error(t.line, t.col, "'await' requires --enable-async flag");
            diag_hint("run with: moxy --enable-async ...");
            diag_bail();
        }
        advance();
        Node *n = node_new(NODE_EXPR_AWAIT);
        n->line = t.line;
        n->col = t.col;
        n->await_expr.inner = parse_postfix();
        return n;
    }

    if (t.kind == TOK_IDENT) {
        Token name = advance();

        if (peek().kind == TOK_COLONCOLON) {
            eat(TOK_COLONCOLON);
            Token variant = eat(TOK_IDENT);

            Node *n = node_new(NODE_EXPR_ENUM_INIT);
            n->line = name.line;
            n->col = name.col;
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

        if (peek().kind == TOK_LPAREN && strcmp(name.text, "print") != 0 &&
            strcmp(name.text, "assert") != 0) {
            eat(TOK_LPAREN);
            Node *n = node_new(NODE_EXPR_CALL);
            n->line = name.line;
            n->col = name.col;
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
        n->line = name.line;
        n->col = name.col;
        strcpy(n->ident.name, name.text);
        return n;
    }

    {
        char msg[256];
        snprintf(msg, sizeof(msg), "unexpected %s in expression", tok_name(t.kind));
        diag_error(t.line, t.col, msg);

        /* suggest fixes for common mistakes */
        if (t.kind == TOK_IDENT) {
            if (strcmp(t.text, "str") == 0)
                diag_hint("did you mean 'string'?");
            else if (strcmp(t.text, "boolean") == 0)
                diag_hint("did you mean 'bool'?");
            else if (strcmp(t.text, "integer") == 0)
                diag_hint("did you mean 'int'?");
            else if (strcmp(t.text, "println") == 0)
                diag_hint("did you mean 'print'?");
            else if (strcmp(t.text, "fn") == 0 || strcmp(t.text, "func") == 0 ||
                     strcmp(t.text, "function") == 0)
                diag_hint("moxy uses C-style function syntax: int add(int a, int b) { ... }");
            else if (strcmp(t.text, "let") == 0 || strcmp(t.text, "var") == 0 ||
                     strcmp(t.text, "val") == 0)
                diag_hint("moxy uses C-style declarations: int x = 42;");
        } else if (t.kind == TOK_FATARROW) {
            diag_hint("'=>' can only be used inside match arms");
        } else if (t.kind == TOK_EOF) {
            diag_hint("unexpected end of file — check for missing '}'");
        }

        diag_bail();
    }
}

static Node *parse_postfix(void) {
    Node *left = parse_primary();

    for (;;) {
        if (peek().kind == TOK_DOT) {
            eat(TOK_DOT);
            Token name = eat(TOK_IDENT);

            if (peek().kind == TOK_LPAREN) {
                eat(TOK_LPAREN);
                Node *n = node_new(NODE_EXPR_METHOD);
                n->line = name.line;
                n->col = name.col;
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
            n->line = name.line;
            n->col = name.col;
            n->field.target = left;
            strcpy(n->field.name, name.text);
            n->field.is_arrow = 0;
            left = n;
            continue;
        }

        if (peek().kind == TOK_ARROW) {
            eat(TOK_ARROW);
            Token name = eat(TOK_IDENT);

            if (peek().kind == TOK_LPAREN) {
                eat(TOK_LPAREN);
                Node *n = node_new(NODE_EXPR_METHOD);
                n->line = name.line;
                n->col = name.col;
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
            n->line = name.line;
            n->col = name.col;
            n->field.target = left;
            strcpy(n->field.name, name.text);
            n->field.is_arrow = 1;
            left = n;
            continue;
        }

        if (peek().kind == TOK_LBRACKET) {
            Token lbt = peek();
            eat(TOK_LBRACKET);
            Node *n = node_new(NODE_EXPR_INDEX);
            n->line = lbt.line;
            n->col = lbt.col;
            n->index.target = left;
            n->index.idx = parse_expr();
            eat(TOK_RBRACKET);
            left = n;
            continue;
        }

        if (peek().kind == TOK_PLUSPLUS || peek().kind == TOK_MINUSMINUS) {
            Token op = advance();
            Node *n = node_new(NODE_EXPR_UNARY);
            n->line = op.line;
            n->col = op.col;
            strcpy(n->unary.op, op.kind == TOK_PLUSPLUS ? "p++" : "p--");
            n->unary.operand = left;
            left = n;
            continue;
        }

        break;
    }

    return left;
}

static Node *parse_expr_prec(int min_prec) {
    Node *left = parse_postfix();

    for (;;) {
        int prec = binop_prec(peek().kind);
        if (prec < min_prec) break;

        if (peek().kind == TOK_PIPEARROW) {
            Token pt = advance();
            Node *right = parse_postfix();

            if (right->kind == NODE_EXPR_CALL) {
                for (int i = right->call.nargs; i > 0; i--)
                    right->call.args[i] = right->call.args[i - 1];
                right->call.args[0] = left;
                right->call.nargs++;
                left = right;
            } else if (right->kind == NODE_EXPR_METHOD) {
                for (int i = right->method.nargs; i > 0; i--)
                    right->method.args[i] = right->method.args[i - 1];
                right->method.args[0] = left;
                right->method.nargs++;
                left = right;
            } else if (right->kind == NODE_EXPR_IDENT) {
                if (strcmp(right->ident.name, "print") == 0) {
                    if (peek().kind == TOK_LPAREN) {
                        eat(TOK_LPAREN);
                        if (peek().kind != TOK_RPAREN) {
                            /* ignore extra args for print pipe */
                            while (peek().kind != TOK_RPAREN) {
                                parse_expr();
                                if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
                            }
                        }
                        eat(TOK_RPAREN);
                    }
                    Node *n = node_new(NODE_PRINT_STMT);
                    n->line = pt.line;
                    n->col = pt.col;
                    n->print_stmt.arg = left;
                    left = n;
                } else {
                    Node *n = node_new(NODE_EXPR_CALL);
                    n->line = pt.line;
                    n->col = pt.col;
                    strcpy(n->call.name, right->ident.name);
                    n->call.args[0] = left;
                    n->call.nargs = 1;
                    if (peek().kind == TOK_LPAREN) {
                        eat(TOK_LPAREN);
                        while (peek().kind != TOK_RPAREN) {
                            n->call.args[n->call.nargs++] = parse_expr();
                            if (peek().kind == TOK_COMMA) eat(TOK_COMMA);
                        }
                        eat(TOK_RPAREN);
                    }
                    left = n;
                }
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "expected function call after '|>'");
                diag_error(pt.line, pt.col, msg);
                diag_hint("pipe operator requires a function call on the right side");
                diag_bail();
            }
            continue;
        }

        Token op = advance();
        Node *right = parse_expr_prec(prec + 1);

        Node *n = node_new(NODE_EXPR_BINOP);
        n->line = op.line;
        n->col = op.col;
        strcpy(n->binop.op, binop_str(op.kind));
        n->binop.left = left;
        n->binop.right = right;
        left = n;
    }

    if (peek().kind == TOK_QUESTION) {
        Token qt = peek();
        advance();
        Node *then_expr = parse_expr();
        eat(TOK_COLON);
        Node *else_expr = parse_expr_prec(1);

        Node *n = node_new(NODE_EXPR_TERNARY);
        n->line = qt.line;
        n->col = qt.col;
        n->ternary.cond = left;
        n->ternary.then_expr = then_expr;
        n->ternary.else_expr = else_expr;
        left = n;
    }

    return left;
}

static Node *parse_expr(void) {
    return parse_expr_prec(0);
}

static Node *parse_print(void) {
    Token pt = peek();
    eat(TOK_IDENT);
    eat(TOK_LPAREN);
    Node *n = node_new(NODE_PRINT_STMT);
    n->line = pt.line;
    n->col = pt.col;
    n->print_stmt.arg = parse_expr();
    eat(TOK_RPAREN);
    if (peek().kind == TOK_SEMI) eat(TOK_SEMI);
    return n;
}

static Node *parse_match(void) {
    Token mt = peek();
    eat(TOK_MATCH_KW);
    Token target = eat(TOK_IDENT);
    eat(TOK_LBRACE);

    Node *n = node_new(NODE_MATCH_STMT);
    n->line = mt.line;
    n->col = mt.col;
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
    Token ift = peek();
    eat(TOK_IF_KW);
    eat(TOK_LPAREN);
    Node *n = node_new(NODE_IF_STMT);
    n->line = ift.line;
    n->col = ift.col;
    n->if_stmt.cond = parse_expr();
    eat(TOK_RPAREN);

    Node *then_block = node_new(NODE_BLOCK);
    then_block->line = ift.line;
    then_block->col = ift.col;
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
            else_block->line = peek().line;
            else_block->col = peek().col;
            else_block->block.nstmts = 1;
            else_block->block.stmts[0] = parse_if_stmt();
            n->if_stmt.else_body = else_block;
            n->if_stmt.nelse = 1;
        } else {
            Node *else_block = node_new(NODE_BLOCK);
            else_block->line = peek().line;
            else_block->col = peek().col;
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
    Token wt = peek();
    eat(TOK_WHILE_KW);
    eat(TOK_LPAREN);
    Node *n = node_new(NODE_WHILE_STMT);
    n->line = wt.line;
    n->col = wt.col;
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

static Node *parse_for_in_stmt(void) {
    Token var1 = eat(TOK_IDENT);
    Node *n = node_new(NODE_FOR_IN_STMT);
    n->line = var1.line;
    n->col = var1.col;
    strcpy(n->for_in_stmt.var1, var1.text);
    n->for_in_stmt.var2[0] = '\0';

    if (peek().kind == TOK_COMMA) {
        eat(TOK_COMMA);
        Token var2 = eat(TOK_IDENT);
        strcpy(n->for_in_stmt.var2, var2.text);
    }

    eat(TOK_IN_KW);

    Node *expr = parse_expr();
    if (peek().kind == TOK_DOTDOT) {
        eat(TOK_DOTDOT);
        Node *range = node_new(NODE_EXPR_RANGE);
        range->line = expr->line;
        range->col = expr->col;
        range->range.start = expr;
        range->range.end = parse_expr();
        n->for_in_stmt.iter = range;
    } else {
        n->for_in_stmt.iter = expr;
    }

    eat(TOK_LBRACE);
    n->for_in_stmt.nbody = 0;
    while (peek().kind != TOK_RBRACE)
        n->for_in_stmt.body[n->for_in_stmt.nbody++] = parse_stmt();
    eat(TOK_RBRACE);
    return n;
}

static Node *parse_for_stmt(void) {
    Token ft = peek();
    eat(TOK_FOR_KW);

    if (peek().kind != TOK_LPAREN)
        return parse_for_in_stmt();

    eat(TOK_LPAREN);
    Node *n = node_new(NODE_FOR_STMT);
    n->line = ft.line;
    n->col = ft.col;

    if (is_type_start(peek())) {
        int save = pos;
        char type[64];
        parse_type(type);
        if (peek().kind == TOK_IDENT) {
            Token name = eat(TOK_IDENT);
            eat(TOK_EQ);
            Node *vd = node_new(NODE_VAR_DECL);
            vd->line = name.line;
            vd->col = name.col;
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

    n->for_stmt.cond = parse_expr();
    eat(TOK_SEMI);

    Node *step_expr = parse_expr();
    if (is_assign_op(peek().kind)) {
        Token op = advance();
        Node *a = node_new(NODE_ASSIGN);
        a->line = op.line;
        a->col = op.col;
        a->assign.target = step_expr;
        assign_op_str(op.kind, a->assign.op);
        a->assign.value = parse_expr();
        n->for_stmt.step = a;
    } else {
        Node *es = node_new(NODE_EXPR_STMT);
        es->line = step_expr->line;
        es->col = step_expr->col;
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
    Token rt = peek();
    eat(TOK_RETURN_KW);
    Node *n = node_new(NODE_RETURN_STMT);
    n->line = rt.line;
    n->col = rt.col;
    if (peek().kind != TOK_SEMI)
        n->return_stmt.value = parse_expr();
    else
        n->return_stmt.value = NULL;
    eat(TOK_SEMI);
    return n;
}

static void check_wrong_keyword(Token t) {
    if (t.kind != TOK_IDENT || toks[pos + 1].kind != TOK_IDENT)
        return;

    int span = (int)strlen(t.text);

    if (strcmp(t.text, "str") == 0) {
        diag_error_span(t.line, t.col, span, "unknown type 'str'");
        diag_hint("did you mean 'string'?");
        diag_bail();
    }
    if (strcmp(t.text, "boolean") == 0) {
        diag_error_span(t.line, t.col, span, "unknown type 'boolean'");
        diag_hint("did you mean 'bool'?");
        diag_bail();
    }
    if (strcmp(t.text, "integer") == 0) {
        diag_error_span(t.line, t.col, span, "unknown type 'integer'");
        diag_hint("did you mean 'int'?");
        diag_bail();
    }
    if (strcmp(t.text, "let") == 0 || strcmp(t.text, "var") == 0 ||
        strcmp(t.text, "val") == 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' is not a moxy keyword", t.text);
        diag_error_span(t.line, t.col, span, msg);
        diag_hint("moxy uses C-style declarations: int x = 42;");
        diag_bail();
    }
    if (strcmp(t.text, "fn") == 0 || strcmp(t.text, "func") == 0 ||
        strcmp(t.text, "function") == 0 || strcmp(t.text, "def") == 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' is not a moxy keyword", t.text);
        diag_error_span(t.line, t.col, span, msg);
        diag_hint("moxy uses C-style function syntax: int add(int a, int b) { ... }");
        diag_bail();
    }
}

static Node *parse_stmt(void) {
    Token t = peek();

    if (t.kind == TOK_IDENT && strcmp(t.text, "print") == 0)
        return parse_print();

    if (t.kind == TOK_IDENT && strcmp(t.text, "assert") == 0) {
        int line = t.line;
        eat(TOK_IDENT);
        eat(TOK_LPAREN);
        Node *n = node_new(NODE_ASSERT_STMT);
        n->line = t.line;
        n->col = t.col;
        n->assert_stmt.arg = parse_expr();
        n->assert_stmt.line = line;
        eat(TOK_RPAREN);
        if (peek().kind == TOK_SEMI) eat(TOK_SEMI);
        return n;
    }

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

    if (t.kind == TOK_IDENT && toks[pos + 1].kind == TOK_COLON)
        return collect_raw_stmt();

    if (t.kind == TOK_SWITCH_KW || t.kind == TOK_DO_KW ||
        t.kind == TOK_GOTO_KW || t.kind == TOK_TYPEDEF_KW ||
        t.kind == TOK_STRUCT_KW || t.kind == TOK_UNION_KW ||
        t.kind == TOK_EXTERN_KW || t.kind == TOK_REGISTER_KW ||
        t.kind == TOK_VOLATILE_KW || t.kind == TOK_INLINE_KW)
        return collect_raw_stmt();

    if (t.kind == TOK_BREAK_KW || t.kind == TOK_CONTINUE_KW ||
        t.kind == TOK_CASE_KW || t.kind == TOK_DEFAULT_KW)
        return collect_raw_stmt();

    check_wrong_keyword(t);

    if (is_type_start(t)) {
        int save = pos;
        char type[64];
        parse_type(type);

        if (peek().kind == TOK_IDENT) {
            Token name_tok = toks[pos];
            TokenKind after = toks[pos + 1].kind;

            if (after == TOK_EQ) {
                eat(TOK_IDENT);
                eat(TOK_EQ);
                Node *n = node_new(NODE_VAR_DECL);
                n->line = name_tok.line;
                n->col = name_tok.col;
                strcpy(n->var_decl.type, type);
                strcpy(n->var_decl.name, name_tok.text);
                n->var_decl.value = parse_expr();
                eat(TOK_SEMI);
                return n;
            }

            if (after == TOK_SEMI) {
                pos = save;
                return collect_raw_stmt();
            }

            if (after == TOK_LBRACKET || after == TOK_COMMA) {
                pos = save;
                return collect_raw_stmt();
            }

            if (after == TOK_LPAREN) {
                pos = save;
            } else {
                pos = save;
            }
        } else {
            pos = save;
        }
    }

    if (!is_expr_start(t))
        return collect_raw_stmt();

    {
        Node *expr = parse_expr();

        if (is_assign_op(peek().kind)) {
            Token op = advance();
            Node *n = node_new(NODE_ASSIGN);
            n->line = op.line;
            n->col = op.col;
            n->assign.target = expr;
            assign_op_str(op.kind, n->assign.op);
            n->assign.value = parse_expr();
            eat(TOK_SEMI);
            return n;
        }

        if (expr->kind == NODE_PRINT_STMT) {
            if (peek().kind == TOK_SEMI) eat(TOK_SEMI);
            return expr;
        }
        Node *n = node_new(NODE_EXPR_STMT);
        n->line = expr->line;
        n->col = expr->col;
        n->expr_stmt.expr = expr;
        if (peek().kind == TOK_SEMI) eat(TOK_SEMI);
        return n;
    }
}

static Node *parse_enum(void) {
    Token et = peek();
    eat(TOK_ENUM_KW);
    Token name = eat(TOK_IDENT);
    eat(TOK_LBRACE);

    Node *n = node_new(NODE_ENUM_DECL);
    n->line = et.line;
    n->col = et.col;
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
    Token fnt = peek();
    eat(TOK_LPAREN);

    Node *n = node_new(NODE_FUNC_DECL);
    n->line = fnt.line;
    n->col = fnt.col;
    strcpy(n->func_decl.ret, ret);
    strcpy(n->func_decl.name, fname);
    n->func_decl.nparams = 0;
    n->func_decl.nbody = 0;

    while (peek().kind != TOK_RPAREN) {
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

static int is_c_enum(void) {
    int save = pos;
    int depth = 1;
    while (toks[pos].kind != TOK_EOF && depth > 0) {
        if (toks[pos].kind == TOK_LBRACE) depth++;
        if (toks[pos].kind == TOK_RBRACE) depth--;
        if (toks[pos].kind == TOK_LPAREN && depth == 1) {
            pos = save;
            return 0; /* Moxy tagged enum */
        }
        if (depth > 0) pos++;
    }
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
    prog->line = 1;
    prog->col = 1;
    prog->program.ndecls = 0;

    while (peek().kind != TOK_EOF) {
        if (peek().kind == TOK_ENUM_KW) {
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
                prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
            }
            continue;
        }

        if (peek().kind == TOK_TYPEDEF_KW ||
            peek().kind == TOK_EXTERN_KW) {
            prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
            continue;
        }

        if (peek().kind == TOK_STRUCT_KW || peek().kind == TOK_UNION_KW) {
            int save = pos;
            advance();
            if (peek().kind == TOK_IDENT) {
                advance(); /* Name */
                if (peek().kind == TOK_LBRACE) {
                    /* struct Name { ... } — could be definition or var */
                    pos = save;
                    prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                    continue;
                }
                pos = save;
            } else if (peek().kind == TOK_LBRACE) {
                pos = save;
                prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                continue;
            } else {
                pos = save;
                prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                continue;
            }
        }

        check_wrong_keyword(peek());

        if (is_type_start(peek())) {
            int save = pos;
            char type[64];
            parse_type(type);

            if (peek().kind == TOK_IDENT) {
                Token name_tok = toks[pos];
                pos++;

                if (peek().kind == TOK_LPAREN) {
                    pos--; /* unconsume ident — parse_func will re-read */
                    Token nm = eat(TOK_IDENT);
                    prog->program.decls[prog->program.ndecls++] =
                        parse_func(type, nm.text);
                } else if (peek().kind == TOK_EQ) {
                    eat(TOK_EQ);
                    Node *n = node_new(NODE_VAR_DECL);
                    n->line = name_tok.line;
                    n->col = name_tok.col;
                    strcpy(n->var_decl.type, type);
                    strcpy(n->var_decl.name, name_tok.text);
                    n->var_decl.value = parse_expr();
                    eat(TOK_SEMI);
                    prog->program.decls[prog->program.ndecls++] = n;
                } else {
                    pos = save;
                    prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
                }
            } else {
                pos = save;
                prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
            }
            continue;
        }

        prog->program.decls[prog->program.ndecls++] = collect_raw_toplevel();
    }

    return prog;
}
