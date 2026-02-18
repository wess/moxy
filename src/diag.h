#ifndef MOXY_DIAG_H
#define MOXY_DIAG_H

#include "token.h"

void diag_init(const char *source, const char *filename);
void diag_error(int line, int col, const char *msg);
void diag_error_span(int line, int col, int span, const char *msg);
void diag_error_expected(int line, int col, TokenKind expected, TokenKind got, const char *got_text);
void diag_warn(int line, int col, const char *msg);
void diag_warn_span(int line, int col, int span, const char *msg);
void diag_hint(const char *msg);
_Noreturn void diag_bail(void);
const char *tok_name(TokenKind kind);

#endif
