#ifndef CINC_AST_H
#define CINC_AST_H

#include "lexer.h"

typedef enum {
    AST_NUMBER,
    AST_IDENTIFIER,

    AST_BINARY,
    AST_UNARY,

    AST_RETURN,
    AST_EXPR_STMT,
    AST_BLOCK,

    AST_FUNCTION,
    AST_VAR_DECL,

    AST_PROGRAM,
} ast_kind_e;

typedef struct ast_node_t ast_node_t;

struct ast_node_t {
    ast_kind_e kind;
    token_t token;

    union {
        // Expressions
        struct {
            long value;
        } number;
        struct {} identifier;
        struct {
            ast_node_t *left;
            ast_node_t *right;
        } binary;
        struct {
            ast_node_t *operand;
        } unary;

        // Statements
        struct {
            ast_node_t *expr;
        } return_stmt;
        struct {
            ast_node_t *expr;
        } expr_stmt;
        struct {
            ast_node_t **stmts;
            unsigned int count;
            unsigned int capacity;
        } block;

        // Declarations
        struct {
            ast_node_t *body;
        } function;

        // Top-level
        struct {
            ast_node_t **declarations;
        } program;
    };
};

ast_node_t *ast_new_number(token_t token);
ast_node_t *ast_new_binary(token_t token, ast_node_t *left, ast_node_t *right);
ast_node_t *ast_new_unary(token_t token, ast_node_t *left);

#endif
