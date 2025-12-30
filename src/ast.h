#ifndef CINC_AST_H
#define CINC_AST_H

#include "lexer.h"

#define AST_KIND_LIST \
    X(AST_NUMBER) \
    X(AST_CHAR) \
    X(AST_STRING) \
    X(AST_IDENTIFIER) \
    \
    X(AST_BINARY) \
    X(AST_UNARY) \
    X(AST_EXPR_STMT) \
    X(AST_RETURN) \
    X(AST_BLOCK) \
    \
    X(AST_FUNCTION) \
    X(AST_VAR_DECL) \
    \
    X(AST_PROGRAM)

typedef enum {
#define X(name) name,
    AST_KIND_LIST
#undef X
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
            token_t name;
            token_t return_type;
        } function;

        // Top-level
        struct {
            ast_node_t **decls;
            unsigned int count;
            unsigned int capacity;
        } program;
    };
};

// Exprs
ast_node_t *ast_new_number(token_t token);
ast_node_t *ast_new_binary(token_t token, ast_node_t *left, ast_node_t *right);
ast_node_t *ast_new_unary(token_t token, ast_node_t *left);

// Stmts
ast_node_t *ast_new_expr_stmt(ast_node_t *expr);
ast_node_t *ast_new_return(token_t token, ast_node_t *expr);
ast_node_t *ast_new_block(token_t token);
void ast_block_append(ast_node_t *block, ast_node_t *stmt);

// Decls
ast_node_t *ast_new_function(token_t name, token_t return_type, ast_node_t *body);

ast_node_t *ast_new_program(void);
void ast_program_append(ast_node_t *program, ast_node_t *decl);

void ast_print(ast_node_t *node, int indent);

#endif
