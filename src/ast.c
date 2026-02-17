#include "ast.h"
#include <stdlib.h>
#include <string.h>

Node *node_new(NodeKind kind) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = kind;
    return n;
}
