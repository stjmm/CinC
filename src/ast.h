#ifndef CINC_AST_H
#define CINC_AST_H

#include "lexer.h"
#include "base/list.h"

#include <stdint.h>

typedef struct ast_expr ast_expr;
typedef struct ast_decl ast_decl;
typedef struct ast_stmt ast_stmt;
typedef struct ast_block_item ast_block_item;
typedef struct ast_program ast_program;

typedef struct type type;
typedef struct symbol symbol;
typedef struct switch_annotation switch_annotation;

/*
 * Expressions
 */

typedef enum {
    EXPR_INT_CONSTANT,
    EXPR_LONG_CONSTANT,
    EXPR_IDENTIFIER,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_PRE,
    EXPR_POST,
    EXPR_ASSIGNMENT,
    EXPR_CONDITIONAL,
    EXPR_CALL,
    EXPR_CAST
} expr_kind;

struct ast_expr {
    expr_kind kind;
    token tok;
    ast_expr *next;

    type *ty;
    bool is_lvalue;

    union {
        int64_t constant_value;

        struct {
            token name;
            symbol *sym;
        } identifier;

        struct {
            token op;
            ast_expr *operand;
        } unary;

        struct {
            token op;
            ast_expr *left;
            ast_expr *right;
        } binary;

        struct {
            token op;
            ast_expr *lvalue;
            ast_expr *rvalue;
        } assignment;

        struct {
            ast_expr *condition;
            ast_expr *then_expr;
            ast_expr *else_expr;
        } conditional;

        struct {
            ast_expr *callee;
            LIST(ast_expr) args;
        } call;

        struct {
            type *target_ty;
            ast_expr *operand;
        } cast;
    };
};

/*
 * Declarations
 */

typedef enum {
    DECL_OBJECT,
    DECL_FUNCTION
} decl_kind;

typedef enum {
    STORAGE_CLASS_NONE,
    STORAGE_CLASS_EXTERN,
    STORAGE_CLASS_STATIC,
    STORAGE_CLASS_AUTO,
    STORAGE_CLASS_REGISTER
} storage_class;

typedef enum {
    STORAGE_DURATION_NONE,
    STORAGE_DURATION_AUTO,
    STORAGE_DURATION_STATIC,
    STORAGE_DURATION_THREAD
} storage_duration;

typedef enum {
    LINKAGE_NONE,
    LINKAGE_INTERNAL,
    LINKAGE_EXTERNAL
} linkage;

struct ast_decl {
    decl_kind kind;
    token tok;
    ast_decl *next;

    type *ty;

    storage_class sc;
    storage_duration sd;
    linkage link;

    bool is_definition;
    bool is_tentative;
    bool is_parameter;

    symbol *sym;
    char *ir_name;

    union {
        struct {
            ast_expr *init;
        } object;

        struct {
            LIST(ast_decl) params;
            ast_stmt *body;
        } function;
    };
};

/*
 * Block items
 */

typedef enum {
    BLOCK_ITEM_DECL,
    BLOCK_ITEM_STMT
} block_item_kind;

struct ast_block_item {
    block_item_kind kind;
    token tok;
    ast_block_item *next;

    union {
        ast_stmt *stmt;
        ast_decl *decl;
    };
};

/*
 * For initializers
 */

typedef enum {
    FOR_INIT_NONE,
    FOR_INIT_EXPR,
    FOR_INIT_DECL
} for_init_kind;

typedef struct {
    for_init_kind kind;

    union {
        ast_expr *expr;
        LIST(ast_decl) decls;
    };
} ast_for_init;

/*
 * Statements
 */

typedef enum {
    STMT_NULL,
    STMT_EXPR,
    STMT_IF,
    STMT_GOTO,
    STMT_LABEL,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_FOR,
    STMT_WHILE,
    STMT_DOWHILE,
    STMT_SWITCH,
    STMT_CASE,
    STMT_DEFAULT,
    STMT_RETURN,
    STMT_BLOCK
} stmt_kind;

struct ast_stmt {
    stmt_kind kind;
    token tok;
    ast_stmt *next;

    union {
        struct {
            ast_expr *expr;
        } expr;

        struct {
            ast_expr *condition;
            ast_stmt *then_stmt;
            ast_stmt *else_stmt;
        } if_stmt;

        struct {
            token label;
        } goto_stmt;

        struct {
            token name;
            ast_stmt *stmt;
        } label;

        struct {
            const char *target_label;
        } break_stmt;

        struct {
            const char *target_label;
        } continue_stmt;

        struct {
            ast_for_init *init;
            ast_expr *condition;
            ast_expr *post;
            ast_stmt *body;

            const char *break_label;
            const char *continue_label;
        } for_stmt;

        struct {
            ast_expr *condition;
            ast_stmt *body;

            const char *break_label;
            const char *continue_label;
        } while_stmt;

        struct {
            ast_expr *condition;
            ast_stmt *body;

            const char *break_label;
            const char *continue_label;
        } dowhile_stmt;

        struct {
            ast_expr *condition;
            ast_stmt *body;

            const char *break_label;
            switch_annotation *annotation;
        } switch_stmt;

        struct {
            ast_expr *value;
            ast_stmt *stmt;

            const char *label;
        } case_stmt;

        struct {
            ast_stmt *stmt;

            const char *label;
        } default_stmt;

        struct {
            ast_expr *expr;
        } return_stmt;

        struct {
            LIST(ast_block_item) items;
        } block;
    };
};

/*
 * Translation unit
 */

struct ast_program{
    LIST(ast_decl) decls;
};

ast_expr *ast_new_expr(expr_kind kind, token tok);
ast_stmt *ast_stmt_new(stmt_kind kind, token tok);
ast_decl *ast_decl_new(decl_kind kind, token tok);
ast_block_item *ast_block_item_new(block_item_kind kind, token tok);
ast_for_init *ast_for_init_new(for_init_kind kind);

#endif
