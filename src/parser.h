#ifndef CINC_PARSER_H
#define CINC_PARSER_H

#include "lexer.h"

typedef enum {
    TYPE_INT,
    TYPE_VOID,
    TYPE_FUNCTION,
} type_kind_e;

typedef struct {
    type_kind_e kind;
} type_t;

typedef enum {
    AST_PROGRAM,
    AST_FUNCTION,
    AST_RETURN,
    AST_BLOCK,

    AST_LITERAL,
} ast_kind_e;

typedef struct ast_node_t ast_node_t;

struct ast_node_t {
    ast_kind_e kind;
    token_t token;
    type_t *resoled_type;

    union {
        // <program>
        struct {
            ast_node_t **functions;
            int count;
        } program;

        // <function>
        struct {
            token_t name;
            type_t *return_type;
            ast_node_t *body;
        } function;

        // <statement>
        struct {
            ast_node_t **stmts;
            int count;
        } block;

        // "return" <exp>
        struct {
            ast_node_t *expr;
        } ret;

        // <int>
        struct {
            int int_val;
        } literal;
    } data;
};

ast_node_t *parse(const char *source);

#endif
