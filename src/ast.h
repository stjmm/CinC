/*
 * AST node definitions for C.
 * Each node has its source token for error reporting.
 * Nodes are heap-allocated and linked via 'next' pointer
 * to form linked lists (block items, program, switch case bodies).
 *
 * Sema annotates nodes in-place:
 *  - token.resolved is set on identifier after variable_resolution
 *  - loop/switch labels are set after label_loops()
 *  - switch_stmt.annotation is set by annotate_switches()
 */

#ifndef CINC_AST_H
#define CINC_AST_H

#include <stdbool.h>

#include "lexer.h"
#include "sema.h"

// Allocates and initializes a new AST node (statement expression)
#define AST_NEW(_type, tok, ...) ({                                               \
    struct ast_node *_n = calloc(1, sizeof(struct ast_node));                     \
    *_n = (struct ast_node){ .type = _type, .token = tok, .next = NULL, __VA_ARGS__ };\
    _n;                                                                           \
    })

#define LIST_APPEND(head, tail, node)  \
    do {                               \
        if (!(head))                   \
            (head) = (node);           \
        else                           \
            (tail)->next = (node);     \
        (tail) = (node);               \
    } while(0)

// X-macro
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
    X(AST_CALL)         \
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
    X(AST_VAR_DECL)     \
    X(AST_FUN_DECL)     \
    /* Top level */     \
    X(AST_PROGRAM)      

enum ast_type {
#define X(ast_name) ast_name,
    AST_NODE_LIST
#undef X
};

struct ast_node {
    enum ast_type type;
    struct token token;      // Primary token for this node
    struct ast_node *next;   // Linked list: next node in block/program etc.

    /*
     * For expressions defined by semantic analysis.
     * For declarations: same as declared type
     */
    struct type *ctype;

    union {
        struct {
            long value;
        } constant;
        struct {
            struct symbol *symbol;
        } identifier;
        struct {
            struct ast_node *expr;
        } unary;
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
        struct {
            struct ast_node *calle;
            struct ast_node *args;
        } call;

        struct {
            struct ast_node *expr;
        } expr_stmt;
        struct {
            struct ast_node *expr;
        } return_stmt;
        struct {
            struct ast_node *condition;
            struct ast_node *then;
            struct ast_node *else_then;
        } if_stmt;
        struct {
            struct token label;
        } goto_stmt;
        struct {
            struct token name;
            struct ast_node *stmt; // Statement to which label points to
        } label_stmt;
        struct {
            struct ast_node *condition;
            struct ast_node *body;
            const char *label; // Unique label for break/continue
        } while_stmt;
        struct {
            struct ast_node *body;
            struct ast_node *condition;
            const char *label;
        } do_while;
        struct {
            struct ast_node *for_init;  // Declaration or expression
            struct ast_node *condition;
            struct ast_node *post;
            struct ast_node *body;
            const char *label;
        } for_stmt;
        struct {
            struct ast_node *value;
            struct ast_node *first; // List of statements in this case
            const char *label;      // Unique label for switch
        } case_stmt;
        struct {
            struct ast_node *first;
            const char *label;
        } default_stmt;
        struct {
            struct ast_node *condition;
            struct ast_node *body;
            const char *label;
            struct switch_annotation *annotation; // Filled by sema, used by IR
        } switch_stmt;
        struct {
            const char *target_label;
        } break_stmt;
        struct {
            const char *target_label;
        } continue_stmt;
        struct {
            struct ast_node *first;
        } block;

        /*
         * This is used for:
         *  file-scope objects
         *  block-scope objects
         *  function parameters
         */
        struct {
            struct token name;
            struct type *type;
            enum storage_class storage;
            struct ast_node *init;

            bool is_parameter;
            bool is_definition;
            bool is_tentative;

            struct symbol *symbol;
        } var_decl;

        /*
         * body == NULL means declaration/prototype
         * body != NULL means function definition
         */
        struct {
            struct token name;
            struct type *type;
            enum storage_class storage;

            struct ast_node *params;
            struct ast_node *body;

            bool is_definition;

            struct symbol *symbol;
        } fun_decl;

        struct { struct ast_node *first; } program;
    };
};

void ast_print(struct ast_node *node, int depth);

#endif
