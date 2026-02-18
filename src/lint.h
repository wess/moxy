#ifndef MOXY_LINT_H
#define MOXY_LINT_H

#include "ast.h"
#include "yaml.h"

int lint_check(Node *program, const MoxyConfig *cfg, const char *source, const char *filename);

#endif
