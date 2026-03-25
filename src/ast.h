#ifndef CINC_AST_H
#define CINC_AST_H

#include "lexer.h"

#define AST_NEW(_kind, tok, ...) ({                                  \
    ast_node_t *_n = calloc(1, sizeof(ast_node_t));                   \
    *_n = (ast_node_t){ .kind = _kind, .token = tok, .next = NULL, __VA_ARGS__ };\
    _n;                                                             \
})

#define AST_NODE_LIST   \
    /* Expressions */   \
    X(AST_CONSTANT)     \
    X(AST_IDENTIFIER)   \
    X(AST_UNARY)        \
    /* Statements */    \
    X(AST_EXPR_STMT)    \
    X(AST_RETURN)       \
    X(AST_BLOCK)        \
    /* Declarations */  \
    X(AST_FUNCTION)     \
    /* Top level */     \
    X(AST_PROGRAM)      

typedef enum {
#define X(ast_name) ast_name,
    AST_NODE_LIST
#undef X
} ast_kind_e;

typedef struct ast_node_t ast_node_t;
struct ast_node_t {
    ast_kind_e kind;
    token_t token;      // Default token
    ast_node_t *next;   // Linked-list for block or program (default NULL)
    union {
        struct { long value; } constant;
        struct {} identifier;
        struct { ast_node_t *expr; } unary;
        struct { ast_node_t *expr; } expr_stmt;
        struct { ast_node_t *expr; } return_stmt;
        struct { ast_node_t *first; } block;
        struct { token_t name; token_t return_type; ast_node_t *body; } function;
        struct { ast_node_t *first; } program;
    };
};

void ast_print(ast_node_t *node, int depth);

#endif
