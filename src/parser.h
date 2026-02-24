#ifndef MOXY_PARSER_H
#define MOXY_PARSER_H

#include "token.h"
#include "ast.h"

Node *parse(Token *tokens, int ntokens);
void parser_register_type(const char *name);

#endif
