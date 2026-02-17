#ifndef MOXY_AST_H
#define MOXY_AST_H

typedef enum {
    NODE_PROGRAM,
    NODE_VAR_DECL,
    NODE_ENUM_DECL,
    NODE_FUNC_DECL,
    NODE_PRINT_STMT,
    NODE_MATCH_STMT,
    NODE_EXPR_STMT,
    NODE_IF_STMT,
    NODE_WHILE_STMT,
    NODE_FOR_STMT,
    NODE_RETURN_STMT,
    NODE_BLOCK,
    NODE_ASSIGN,
    NODE_EXPR_IDENT,
    NODE_EXPR_INTLIT,
    NODE_EXPR_FLOATLIT,
    NODE_EXPR_STRLIT,
    NODE_EXPR_CHARLIT,
    NODE_EXPR_BOOLLIT,
    NODE_EXPR_NULL,
    NODE_EXPR_ENUM_INIT,
    NODE_EXPR_LIST_LIT,
    NODE_EXPR_OK,
    NODE_EXPR_ERR,
    NODE_EXPR_METHOD,
    NODE_EXPR_FIELD,
    NODE_EXPR_INDEX,
    NODE_EXPR_EMPTY,
    NODE_EXPR_CALL,
    NODE_EXPR_BINOP,
    NODE_EXPR_UNARY,
    NODE_EXPR_PAREN,
} NodeKind;

typedef struct Node Node;

typedef struct {
    char name[64];
    char type[64];
} Field;

typedef struct {
    char name[64];
    Field fields[8];
    int nfields;
} Variant;

typedef struct {
    char enum_name[64];
    char variant[64];
    char binding[64];
} Pattern;

typedef struct {
    Pattern pattern;
    Node *body;
} MatchArm;

typedef struct {
    char type[64];
    char name[64];
} Param;

struct Node {
    NodeKind kind;
    union {
        struct { Node *decls[64]; int ndecls; } program;
        struct { char type[64]; char name[64]; Node *value; } var_decl;
        struct { char name[64]; Variant variants[16]; int nvariants; } enum_decl;
        struct { char ret[64]; char name[64]; Param params[16]; int nparams; Node *body[256]; int nbody; } func_decl;
        struct { Node *arg; } print_stmt;
        struct { char target[64]; MatchArm arms[16]; int narms; } match_stmt;
        struct { Node *expr; } expr_stmt;
        struct { Node *cond; Node *then_body; int nthen; Node *else_body; int nelse; } if_stmt;
        struct { Node *cond; Node *body[256]; int nbody; } while_stmt;
        struct { Node *init; Node *cond; Node *step; Node *body[256]; int nbody; } for_stmt;
        struct { Node *value; } return_stmt;
        struct { Node *stmts[256]; int nstmts; } block;
        struct { Node *target; char op[4]; Node *value; } assign;
        struct { char name[64]; } ident;
        struct { int value; } intlit;
        struct { char value[64]; } floatlit;
        struct { char value[256]; } strlit;
        struct { char value; } charlit;
        struct { int value; } boollit;
        struct { char ename[64]; char vname[64]; Node *args[8]; int nargs; } enum_init;
        struct { Node *items[64]; int nitems; } list_lit;
        struct { Node *inner; } ok_expr;
        struct { Node *inner; } err_expr;
        struct { Node *target; char name[64]; Node *args[8]; int nargs; } method;
        struct { Node *target; char name[64]; } field;
        struct { Node *target; Node *idx; } index;
        struct { char name[64]; Node *args[16]; int nargs; } call;
        struct { char op[4]; Node *left; Node *right; } binop;
        struct { char op[4]; Node *operand; } unary;
        struct { Node *inner; } paren;
    };
};

Node *node_new(NodeKind kind);

#endif
