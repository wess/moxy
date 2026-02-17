#ifndef MOXY_LEXER_H
#define MOXY_LEXER_H

#include "token.h"

typedef struct {
    const char *src;
    int pos;
    int line;
    int col;
} Lexer;

void lexer_init(Lexer *l, const char *src);
Token lexer_next(Lexer *l);

#endif
