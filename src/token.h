#ifndef MOXY_TOKEN_H
#define MOXY_TOKEN_H

typedef enum {
    TOK_STRING_KW,
    TOK_INT_KW,
    TOK_FLOAT_KW,
    TOK_DOUBLE_KW,
    TOK_CHAR_KW,
    TOK_BOOL_KW,
    TOK_LONG_KW,
    TOK_SHORT_KW,
    TOK_VOID_KW,
    TOK_ENUM_KW,
    TOK_MATCH_KW,
    TOK_TRUE_KW,
    TOK_FALSE_KW,
    TOK_RESULT_KW,
    TOK_MAP_KW,
    TOK_OK_KW,
    TOK_ERR_KW,
    TOK_IF_KW,
    TOK_ELSE_KW,
    TOK_FOR_KW,
    TOK_WHILE_KW,
    TOK_RETURN_KW,
    TOK_NULL_KW,

    TOK_IDENT,
    TOK_STRLIT,
    TOK_INTLIT,
    TOK_FLOATLIT,
    TOK_CHARLIT,

    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LT,
    TOK_GT,
    TOK_DOT,
    TOK_COMMA,
    TOK_SEMI,
    TOK_EQ,
    TOK_COLONCOLON,
    TOK_FATARROW,

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQEQ,
    TOK_NEQ,
    TOK_LTEQ,
    TOK_GTEQ,
    TOK_AND,
    TOK_OR,
    TOK_BANG,
    TOK_PLUSEQ,
    TOK_MINUSEQ,
    TOK_STAREQ,
    TOK_SLASHEQ,
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,

    TOK_EOF,
} TokenKind;

typedef struct {
    TokenKind kind;
    char text[256];
    int line;
    int col;
} Token;

#endif
