#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *src;
static const char *fname;

void diag_init(const char *source, const char *filename) {
    src = source;
    fname = filename;
}

static const char *line_start(int line) {
    if (!src) return NULL;
    const char *p = src;
    int cur = 1;
    while (*p && cur < line) {
        if (*p == '\n') cur++;
        p++;
    }
    return (cur == line) ? p : NULL;
}

static int line_len(const char *start) {
    int len = 0;
    while (start[len] && start[len] != '\n') len++;
    return len;
}

static int digit_width(int n) {
    if (n < 10) return 1;
    if (n < 100) return 2;
    if (n < 1000) return 3;
    return 4;
}

static void show_source(int line, int col, int span) {
    const char *ls = line_start(line);
    if (!ls) return;

    int len = line_len(ls);
    int w = digit_width(line);

    fprintf(stderr, " %*s |\n", w, "");

    fprintf(stderr, " %*d | ", w, line);
    fwrite(ls, 1, len, stderr);
    fprintf(stderr, "\n");

    fprintf(stderr, " %*s | ", w, "");
    int caret_pos = (col > 0) ? col - 1 : 0;
    for (int i = 0; i < caret_pos; i++) {
        if (i < len && ls[i] == '\t') fprintf(stderr, "\t");
        else fprintf(stderr, " ");
    }
    for (int i = 0; i < span && i < 40; i++)
        fprintf(stderr, "^");
    fprintf(stderr, "\n");
}

void diag_error(int line, int col, const char *msg) {
    fprintf(stderr, "\033[1;31merror\033[0m\033[1m: %s\033[0m\n", msg);
    if (fname)
        fprintf(stderr, "  \033[1;34m-->\033[0m %s:%d:%d\n", fname, line, col);
    show_source(line, col, 1);
}

void diag_error_span(int line, int col, int span, const char *msg) {
    fprintf(stderr, "\033[1;31merror\033[0m\033[1m: %s\033[0m\n", msg);
    if (fname)
        fprintf(stderr, "  \033[1;34m-->\033[0m %s:%d:%d\n", fname, line, col);
    show_source(line, col, span > 0 ? span : 1);
}

void diag_error_expected(int line, int col, TokenKind expected, TokenKind got, const char *got_text) {
    char msg[256];
    int got_len = (int)strlen(got_text);

    snprintf(msg, sizeof(msg), "expected %s, found %s",
             tok_name(expected), tok_name(got));

    fprintf(stderr, "\033[1;31merror\033[0m\033[1m: %s\033[0m\n", msg);
    if (fname)
        fprintf(stderr, "  \033[1;34m-->\033[0m %s:%d:%d\n", fname, line, col);
    show_source(line, col, got_len > 0 ? got_len : 1);

    if (expected == TOK_SEMI && got == TOK_COMMA) {
        diag_hint("in match arms, wrap statements in braces: { statement; }");
    } else if (expected == TOK_SEMI && got == TOK_RBRACE) {
        diag_hint("add ';' before '}'");
    } else if (expected == TOK_SEMI) {
        diag_hint("add ';' at end of statement");
    } else if (expected == TOK_LBRACE && got == TOK_EQ) {
        diag_hint("function bodies must be wrapped in { }");
    } else if (expected == TOK_RPAREN) {
        diag_hint("unclosed '(' — add ')' to match");
    } else if (expected == TOK_RBRACKET) {
        diag_hint("unclosed '[' — add ']' to match");
    } else if (expected == TOK_RBRACE) {
        diag_hint("unclosed '{' — add '}' to match");
    } else if (expected == TOK_LPAREN && got == TOK_IDENT) {
        diag_hint("expected '(' after function name");
    }
}

void diag_warn(int line, int col, const char *msg) {
    fprintf(stderr, "\033[1;33mwarning\033[0m\033[1m: %s\033[0m\n", msg);
    if (fname)
        fprintf(stderr, "  \033[1;34m-->\033[0m %s:%d:%d\n", fname, line, col);
    show_source(line, col, 1);
}

void diag_warn_span(int line, int col, int span, const char *msg) {
    fprintf(stderr, "\033[1;33mwarning\033[0m\033[1m: %s\033[0m\n", msg);
    if (fname)
        fprintf(stderr, "  \033[1;34m-->\033[0m %s:%d:%d\n", fname, line, col);
    show_source(line, col, span > 0 ? span : 1);
}

void diag_hint(const char *msg) {
    fprintf(stderr, "  \033[1;32m= help\033[0m: %s\n", msg);
}

_Noreturn void diag_bail(void) {
    exit(1);
}

const char *tok_name(TokenKind kind) {
    switch (kind) {
    case TOK_STRING_KW:  return "'string'";
    case TOK_INT_KW:     return "'int'";
    case TOK_FLOAT_KW:   return "'float'";
    case TOK_DOUBLE_KW:  return "'double'";
    case TOK_CHAR_KW:    return "'char'";
    case TOK_BOOL_KW:    return "'bool'";
    case TOK_LONG_KW:    return "'long'";
    case TOK_SHORT_KW:   return "'short'";
    case TOK_VOID_KW:    return "'void'";
    case TOK_ENUM_KW:    return "'enum'";
    case TOK_MATCH_KW:   return "'match'";
    case TOK_TRUE_KW:    return "'true'";
    case TOK_FALSE_KW:   return "'false'";
    case TOK_RESULT_KW:  return "'Result'";
    case TOK_MAP_KW:     return "'map'";
    case TOK_OK_KW:      return "'Ok'";
    case TOK_ERR_KW:     return "'Err'";
    case TOK_IF_KW:      return "'if'";
    case TOK_ELSE_KW:    return "'else'";
    case TOK_FOR_KW:     return "'for'";
    case TOK_WHILE_KW:   return "'while'";
    case TOK_RETURN_KW:  return "'return'";
    case TOK_NULL_KW:    return "'null'";
    case TOK_IN_KW:      return "'in'";
    case TOK_FUTURE_KW:  return "'Future'";
    case TOK_AWAIT_KW:   return "'await'";
    case TOK_STRUCT_KW:  return "'struct'";
    case TOK_UNION_KW:   return "'union'";
    case TOK_TYPEDEF_KW: return "'typedef'";
    case TOK_SWITCH_KW:  return "'switch'";
    case TOK_CASE_KW:    return "'case'";
    case TOK_DEFAULT_KW: return "'default'";
    case TOK_DO_KW:      return "'do'";
    case TOK_BREAK_KW:   return "'break'";
    case TOK_CONTINUE_KW:return "'continue'";
    case TOK_SIZEOF_KW:  return "'sizeof'";
    case TOK_STATIC_KW:  return "'static'";
    case TOK_CONST_KW:   return "'const'";
    case TOK_EXTERN_KW:  return "'extern'";
    case TOK_UNSIGNED_KW:return "'unsigned'";
    case TOK_SIGNED_KW:  return "'signed'";
    case TOK_GOTO_KW:    return "'goto'";
    case TOK_VOLATILE_KW:return "'volatile'";
    case TOK_REGISTER_KW:return "'register'";
    case TOK_INLINE_KW:  return "'inline'";
    case TOK_IDENT:      return "identifier";
    case TOK_STRLIT:     return "string literal";
    case TOK_INTLIT:     return "integer literal";
    case TOK_FLOATLIT:   return "float literal";
    case TOK_CHARLIT:    return "char literal";
    case TOK_LBRACE:     return "'{'";
    case TOK_RBRACE:     return "'}'";
    case TOK_LPAREN:     return "'('";
    case TOK_RPAREN:     return "')'";
    case TOK_LBRACKET:   return "'['";
    case TOK_RBRACKET:   return "']'";
    case TOK_LT:         return "'<'";
    case TOK_GT:         return "'>'";
    case TOK_DOT:        return "'.'";
    case TOK_COMMA:      return "','";
    case TOK_SEMI:       return "';'";
    case TOK_EQ:         return "'='";
    case TOK_COLONCOLON: return "'::'";
    case TOK_FATARROW:   return "'=>'";
    case TOK_COLON:      return "':'";
    case TOK_QUESTION:   return "'?'";
    case TOK_PLUS:       return "'+'";
    case TOK_MINUS:      return "'-'";
    case TOK_STAR:       return "'*'";
    case TOK_SLASH:      return "'/'";
    case TOK_PERCENT:    return "'%'";
    case TOK_EQEQ:       return "'=='";
    case TOK_NEQ:        return "'!='";
    case TOK_LTEQ:       return "'<='";
    case TOK_GTEQ:       return "'>='";
    case TOK_AND:        return "'&&'";
    case TOK_OR:         return "'||'";
    case TOK_BANG:       return "'!'";
    case TOK_PLUSEQ:     return "'+='";
    case TOK_MINUSEQ:    return "'-='";
    case TOK_STAREQ:     return "'*='";
    case TOK_SLASHEQ:    return "'/='";
    case TOK_PLUSPLUS:   return "'++'";
    case TOK_MINUSMINUS: return "'--'";
    case TOK_AMP:        return "'&'";
    case TOK_PIPE:       return "'|'";
    case TOK_CARET:      return "'^'";
    case TOK_TILDE:      return "'~'";
    case TOK_ARROW:      return "'->'";
    case TOK_LSHIFT:     return "'<<'";
    case TOK_RSHIFT:     return "'>>'";
    case TOK_AMPEQ:      return "'&='";
    case TOK_PIPEEQ:     return "'|='";
    case TOK_PIPEARROW:  return "'|>'";
    case TOK_CARETEQ:    return "'^='";
    case TOK_PERCENTEQ:  return "'%='";
    case TOK_LSHIFTEQ:   return "'<<='";
    case TOK_RSHIFTEQ:   return "'>>='";
    case TOK_ELLIPSIS:   return "'...'";
    case TOK_DOTDOT:     return "'..'";
    case TOK_UNKNOWN:    return "unknown character";
    case TOK_EOF:        return "end of file";
    }
    return "unknown";
}
