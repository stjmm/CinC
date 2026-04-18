#ifndef CINC_AST_H
#define CINC_AST_H

#include "lexer.h"

#define AST_NEW(_type, tok, ...) ({                                  \
    struct ast_node *_n = calloc(1, sizeof(struct ast_node));                   \
    *_n = (struct ast_node){ .type = _type, .token = tok, .next = NULL, __VA_ARGS__ };\
    _n;                                                             \
})

#define AST_NODE_LIST   \
    /* Expressions */   \
    X(AST_CONSTANT)     \
    X(AST_IDENTIFIER)   \
    X(AST_UNARY)        \
    X(AST_BINARY)       \
    X(AST_PRE)          \
    X(AST_POST)         \
    X(AST_ASSIGNMENT)   \
    X(AST_TERNARY)      \
    /* Statements */    \
    X(AST_EXPR_STMT)    \
    X(AST_NULL_STMT)    \
    X(AST_RETURN)       \
    X(AST_IF_STMT)      \
    X(AST_BLOCK)        \
    X(AST_GOTO)         \
    X(AST_LABEL_STMT)   \
    X(AST_BREAK)        \
    X(AST_CONTINUE)     \
    X(AST_WHILE)        \
    X(AST_DOWHILE)      \
    X(AST_FOR)          \
    X(AST_SWITCH)       \
    X(AST_CASE)         \
    X(AST_DEFAULT)      \
    /* Declarations */  \
    X(AST_DECLARATION)  \
    X(AST_FUNCTION)     \
    /* Top level */     \
    X(AST_PROGRAM)      

enum ast_type {
#define X(ast_name) ast_name,
    AST_NODE_LIST
#undef X
};

struct ast_node {
    enum ast_type type;
    struct token token;      // Default token
    struct ast_node *next;   // Linked-list for block or program (default NULL)
    union {
        struct { long value; } constant;
        struct { struct ast_node *expr; } unary;
        struct {
            struct ast_node *left;
            struct ast_node *right;
        } binary;
        struct {
            struct ast_node *lvalue;
            struct ast_node *rvalue;
        } assignment;
        struct {
            struct ast_node *condition;
            struct ast_node *then;
            struct ast_node *else_then;
        } ternary;

        struct { struct ast_node *expr; } expr_stmt;
        struct { struct ast_node *expr; } return_stmt;
        struct {
            struct ast_node *condition;
            struct ast_node *then;
            struct ast_node *else_then;
        } if_stmt;
        struct { struct token label; } goto_stmt;
        struct {
            struct token name;
        } label_stmt;
        struct {
            struct ast_node *condition;
            struct ast_node *body;
            const char *label;
        } while_stmt;
        struct {
            struct ast_node *body;
            struct ast_node *condition;
            const char *label;
        } do_while;
        struct {
            struct ast_node *for_init;
            struct ast_node *condition;
            struct ast_node *post;
            struct ast_node *body;
            const char *label;
        } for_stmt;
        struct {
            struct ast_node *value;
            struct ast_node *first;
            const char *label;
        } case_stmt;
        struct {
            struct ast_node *first;
            const char *label;
        } default_stmt;
        struct {
            struct ast_node *condition;
            struct ast_node *cases; // List of AST_CASE / AST_DEFAULT
            const char *label;
            struct switch_annotation *annotation;
        } switch_stmt;
        struct { const char *target_label; } break_stmt;
        struct { const char *target_label; } continue_stmt;
        struct { struct ast_node *first; } block;

        struct {
            struct ast_node *name;
            struct ast_node *init;
        } declaration;
        struct {
            struct ast_node *name;
            struct ast_node *body;
            struct token return_type;
        } function;

        struct { struct ast_node *first; } program;
    };
};

void ast_print(struct ast_node *node, int depth);

#endif
