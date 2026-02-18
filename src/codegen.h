#ifndef MOXY_CODEGEN_H
#define MOXY_CODEGEN_H

#include "ast.h"

const char *codegen(Node *program);
void codegen_add_include(const char *line);
void codegen_add_directive(const char *line);
void codegen_reset_includes(void);

#endif
