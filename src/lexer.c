#include "lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

void lexer_init(Lexer *l, const char *src) {
    l->src = src;
    l->pos = 0;
    l->line = 1;
    l->col = 1;
}

static char peek(Lexer *l) {
    return l->src[l->pos];
}

static char peek2(Lexer *l) {
    return l->src[l->pos + 1];
}

static char peek3(Lexer *l) {
    return l->src[l->pos + 2];
}

static char advance(Lexer *l) {
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else { l->col++; }
    return c;
}

static void skip_ws(Lexer *l) {
    for (;;) {
        char c = peek(l);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance(l);
            continue;
        }
        if (c == '/' && peek2(l) == '/') {
            while (peek(l) && peek(l) != '\n') advance(l);
            continue;
        }
        if (c == '/' && peek2(l) == '*') {
            advance(l); advance(l);
            while (peek(l) && !(peek(l) == '*' && peek2(l) == '/'))
                advance(l);
            if (peek(l)) { advance(l); advance(l); }
            continue;
        }
        break;
    }
}

static Token tok(TokenKind kind, const char *text, int line, int col) {
    Token t;
    t.kind = kind;
    t.line = line;
    t.col = col;
    strncpy(t.text, text, sizeof(t.text) - 1);
    t.text[sizeof(t.text) - 1] = '\0';
    return t;
}

static TokenKind keyword(const char *w) {
    if (strcmp(w, "string") == 0) return TOK_STRING_KW;
    if (strcmp(w, "int") == 0) return TOK_INT_KW;
    if (strcmp(w, "float") == 0) return TOK_FLOAT_KW;
    if (strcmp(w, "double") == 0) return TOK_DOUBLE_KW;
    if (strcmp(w, "char") == 0) return TOK_CHAR_KW;
    if (strcmp(w, "bool") == 0) return TOK_BOOL_KW;
    if (strcmp(w, "long") == 0) return TOK_LONG_KW;
    if (strcmp(w, "short") == 0) return TOK_SHORT_KW;
    if (strcmp(w, "void") == 0) return TOK_VOID_KW;
    if (strcmp(w, "enum") == 0) return TOK_ENUM_KW;
    if (strcmp(w, "match") == 0) return TOK_MATCH_KW;
    if (strcmp(w, "true") == 0) return TOK_TRUE_KW;
    if (strcmp(w, "false") == 0) return TOK_FALSE_KW;
    if (strcmp(w, "Result") == 0) return TOK_RESULT_KW;
    if (strcmp(w, "map") == 0) return TOK_MAP_KW;
    if (strcmp(w, "Ok") == 0) return TOK_OK_KW;
    if (strcmp(w, "Err") == 0) return TOK_ERR_KW;
    if (strcmp(w, "if") == 0) return TOK_IF_KW;
    if (strcmp(w, "else") == 0) return TOK_ELSE_KW;
    if (strcmp(w, "for") == 0) return TOK_FOR_KW;
    if (strcmp(w, "while") == 0) return TOK_WHILE_KW;
    if (strcmp(w, "return") == 0) return TOK_RETURN_KW;
    if (strcmp(w, "null") == 0) return TOK_NULL_KW;
    if (strcmp(w, "struct") == 0) return TOK_STRUCT_KW;
    if (strcmp(w, "union") == 0) return TOK_UNION_KW;
    if (strcmp(w, "typedef") == 0) return TOK_TYPEDEF_KW;
    if (strcmp(w, "switch") == 0) return TOK_SWITCH_KW;
    if (strcmp(w, "case") == 0) return TOK_CASE_KW;
    if (strcmp(w, "default") == 0) return TOK_DEFAULT_KW;
    if (strcmp(w, "do") == 0) return TOK_DO_KW;
    if (strcmp(w, "break") == 0) return TOK_BREAK_KW;
    if (strcmp(w, "continue") == 0) return TOK_CONTINUE_KW;
    if (strcmp(w, "sizeof") == 0) return TOK_SIZEOF_KW;
    if (strcmp(w, "static") == 0) return TOK_STATIC_KW;
    if (strcmp(w, "const") == 0) return TOK_CONST_KW;
    if (strcmp(w, "extern") == 0) return TOK_EXTERN_KW;
    if (strcmp(w, "unsigned") == 0) return TOK_UNSIGNED_KW;
    if (strcmp(w, "signed") == 0) return TOK_SIGNED_KW;
    if (strcmp(w, "goto") == 0) return TOK_GOTO_KW;
    if (strcmp(w, "volatile") == 0) return TOK_VOLATILE_KW;
    if (strcmp(w, "register") == 0) return TOK_REGISTER_KW;
    if (strcmp(w, "inline") == 0) return TOK_INLINE_KW;
    if (strcmp(w, "NULL") == 0) return TOK_NULL_KW;
    if (strcmp(w, "in") == 0) return TOK_IN_KW;
    if (strcmp(w, "Future") == 0) return TOK_FUTURE_KW;
    if (strcmp(w, "await") == 0) return TOK_AWAIT_KW;
    return TOK_IDENT;
}

Token lexer_next(Lexer *l) {
    skip_ws(l);

    int line = l->line;
    int col = l->col;
    char c = peek(l);

    if (c == '\0') return tok(TOK_EOF, "", line, col);

    if (c == '"') {
        advance(l);
        int start = l->pos;
        while (peek(l) && peek(l) != '"') {
            if (peek(l) == '\\') advance(l);
            if (peek(l)) advance(l);
        }
        int len = l->pos - start;
        char buf[256] = {0};
        if (len > 255) len = 255;
        memcpy(buf, l->src + start, len);
        advance(l);
        return tok(TOK_STRLIT, buf, line, col);
    }

    if (isdigit(c)) {
        int start = l->pos;
        int is_float = 0;

        if (c == '0' && (peek2(l) == 'x' || peek2(l) == 'X')) {
            advance(l); advance(l);
            while (isxdigit(peek(l))) advance(l);
        } else {
            while (isdigit(peek(l))) advance(l);
            if (peek(l) == '.' && isdigit(l->src[l->pos + 1])) {
                is_float = 1;
                advance(l);
                while (isdigit(peek(l))) advance(l);
            }
            if (peek(l) == 'e' || peek(l) == 'E') {
                is_float = 1;
                advance(l);
                if (peek(l) == '+' || peek(l) == '-') advance(l);
                while (isdigit(peek(l))) advance(l);
            }
        }

        while (peek(l) == 'L' || peek(l) == 'l' ||
               peek(l) == 'U' || peek(l) == 'u' ||
               peek(l) == 'f' || peek(l) == 'F') {
            if (peek(l) == 'f' || peek(l) == 'F') is_float = 1;
            advance(l);
        }

        int len = l->pos - start;
        char buf[64] = {0};
        if (len > 63) len = 63;
        memcpy(buf, l->src + start, len);
        return tok(is_float ? TOK_FLOATLIT : TOK_INTLIT, buf, line, col);
    }

    if (c == '\'') {
        advance(l);
        int start = l->pos;
        if (peek(l) == '\\') {
            advance(l);
            advance(l);
        } else {
            advance(l);
        }
        int len = l->pos - start;
        char buf[8] = {0};
        if (len > 7) len = 7;
        memcpy(buf, l->src + start, len);
        if (peek(l) == '\'') advance(l);
        return tok(TOK_CHARLIT, buf, line, col);
    }

    if (isalpha(c) || c == '_') {
        int start = l->pos;
        while (isalnum(peek(l)) || peek(l) == '_') advance(l);
        int len = l->pos - start;
        char buf[256] = {0};
        if (len > 255) len = 255;
        memcpy(buf, l->src + start, len);
        return tok(keyword(buf), buf, line, col);
    }

    char c2 = peek2(l);
    char c3 = peek3(l);

    if (c == '<' && c2 == '<' && c3 == '=') { advance(l); advance(l); advance(l); return tok(TOK_LSHIFTEQ, "<<=", line, col); }
    if (c == '>' && c2 == '>' && c3 == '=') { advance(l); advance(l); advance(l); return tok(TOK_RSHIFTEQ, ">>=", line, col); }
    if (c == '.' && c2 == '.' && c3 == '.') { advance(l); advance(l); advance(l); return tok(TOK_ELLIPSIS, "...", line, col); }
    if (c == '.' && c2 == '.' && c3 != '.') { advance(l); advance(l); return tok(TOK_DOTDOT, "..", line, col); }

    if (c == ':' && c2 == ':') { advance(l); advance(l); return tok(TOK_COLONCOLON, "::", line, col); }
    if (c == '=' && c2 == '>') { advance(l); advance(l); return tok(TOK_FATARROW, "=>", line, col); }
    if (c == '=' && c2 == '=') { advance(l); advance(l); return tok(TOK_EQEQ, "==", line, col); }
    if (c == '!' && c2 == '=') { advance(l); advance(l); return tok(TOK_NEQ, "!=", line, col); }
    if (c == '<' && c2 == '<') { advance(l); advance(l); return tok(TOK_LSHIFT, "<<", line, col); }
    if (c == '<' && c2 == '=') { advance(l); advance(l); return tok(TOK_LTEQ, "<=", line, col); }
    if (c == '>' && c2 == '>') { advance(l); advance(l); return tok(TOK_RSHIFT, ">>", line, col); }
    if (c == '>' && c2 == '=') { advance(l); advance(l); return tok(TOK_GTEQ, ">=", line, col); }
    if (c == '&' && c2 == '&') { advance(l); advance(l); return tok(TOK_AND, "&&", line, col); }
    if (c == '&' && c2 == '=') { advance(l); advance(l); return tok(TOK_AMPEQ, "&=", line, col); }
    if (c == '|' && c2 == '|') { advance(l); advance(l); return tok(TOK_OR, "||", line, col); }
    if (c == '|' && c2 == '>') { advance(l); advance(l); return tok(TOK_PIPEARROW, "|>", line, col); }
    if (c == '|' && c2 == '=') { advance(l); advance(l); return tok(TOK_PIPEEQ, "|=", line, col); }
    if (c == '^' && c2 == '=') { advance(l); advance(l); return tok(TOK_CARETEQ, "^=", line, col); }
    if (c == '%' && c2 == '=') { advance(l); advance(l); return tok(TOK_PERCENTEQ, "%=", line, col); }
    if (c == '-' && c2 == '>') { advance(l); advance(l); return tok(TOK_ARROW, "->", line, col); }
    if (c == '+' && c2 == '=') { advance(l); advance(l); return tok(TOK_PLUSEQ, "+=", line, col); }
    if (c == '-' && c2 == '=') { advance(l); advance(l); return tok(TOK_MINUSEQ, "-=", line, col); }
    if (c == '*' && c2 == '=') { advance(l); advance(l); return tok(TOK_STAREQ, "*=", line, col); }
    if (c == '/' && c2 == '=') { advance(l); advance(l); return tok(TOK_SLASHEQ, "/=", line, col); }
    if (c == '+' && c2 == '+') { advance(l); advance(l); return tok(TOK_PLUSPLUS, "++", line, col); }
    if (c == '-' && c2 == '-') { advance(l); advance(l); return tok(TOK_MINUSMINUS, "--", line, col); }

    advance(l);
    switch (c) {
        case '{': return tok(TOK_LBRACE, "{", line, col);
        case '}': return tok(TOK_RBRACE, "}", line, col);
        case '(': return tok(TOK_LPAREN, "(", line, col);
        case ')': return tok(TOK_RPAREN, ")", line, col);
        case ',': return tok(TOK_COMMA, ",", line, col);
        case ';': return tok(TOK_SEMI, ";", line, col);
        case '=': return tok(TOK_EQ, "=", line, col);
        case '[': return tok(TOK_LBRACKET, "[", line, col);
        case ']': return tok(TOK_RBRACKET, "]", line, col);
        case '<': return tok(TOK_LT, "<", line, col);
        case '>': return tok(TOK_GT, ">", line, col);
        case '.': return tok(TOK_DOT, ".", line, col);
        case '+': return tok(TOK_PLUS, "+", line, col);
        case '-': return tok(TOK_MINUS, "-", line, col);
        case '*': return tok(TOK_STAR, "*", line, col);
        case '/': return tok(TOK_SLASH, "/", line, col);
        case '%': return tok(TOK_PERCENT, "%", line, col);
        case '!': return tok(TOK_BANG, "!", line, col);
        case ':': return tok(TOK_COLON, ":", line, col);
        case '?': return tok(TOK_QUESTION, "?", line, col);
        case '&': return tok(TOK_AMP, "&", line, col);
        case '|': return tok(TOK_PIPE, "|", line, col);
        case '^': return tok(TOK_CARET, "^", line, col);
        case '~': return tok(TOK_TILDE, "~", line, col);
    }

    char unk[4] = {c, '\0'};
    return tok(TOK_UNKNOWN, unk, line, col);
}
