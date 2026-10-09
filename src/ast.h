#ifndef CINC_AST_H
#define CINC_AST_H

#include "lexer.h"
#include "base/list.h"

#include <stdint.h>

typedef struct ast_expr_t ast_expr_t;
typedef struct ast_decl_t ast_decl_t;
typedef struct ast_stmt_t ast_stmt_t;
typedef struct ast_program_t ast_program_t;

typedef struct type_t type_t;
typedef struct symbol_t symbol_t;

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

struct ast_expr_t {
    expr_kind kind;
    token_t tok;
    ast_expr_t *next;

    /* Sema filled */
    type_t *ty;
    bool is_lvalue;

    union {
        int64_t constant_value;

        struct {
            token_t name;
            symbol_t *sym;
        } identifier;

        struct {
            token_t op;
            ast_expr_t *operand;
        } unary;

        struct {
            token_t op;
            ast_expr_t *left;
            ast_expr_t *right;
        } binary;

        struct {
            token_t op;
            type_t *op_ty;
            ast_expr_t *lvalue;
            ast_expr_t *rvalue;
        } assignment;

        struct {
            ast_expr_t *condition;
            ast_expr_t *then_expr;
            ast_expr_t *else_expr;
        } conditional;

        struct {
            ast_expr_t *callee;
            LIST(ast_expr_t) args;
        } call;

        struct {
            type_t *target_ty;
            ast_expr_t *operand;
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

struct ast_decl_t {
    decl_kind kind;
    token_t name;
    ast_decl_t *next;

    type_t *ty;

    /* Parsed */
    storage_class sc;
    bool is_parameter;

    /* Sema filled */
    storage_duration sd;
    linkage link;

    bool is_definition;
    bool is_tentative;

    symbol_t *sym;

    union {
        struct {
            ast_expr_t *init;
        } object;

        struct {
            LIST(ast_decl_t) params;
            ast_stmt_t *body;
        } function;
    };
};

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
    STMT_BLOCK,
    STMT_DECL
} stmt_kind;

struct ast_stmt_t {
    stmt_kind kind;
    token_t tok;
    ast_stmt_t *next;

    /* Unique id for labels */
    uint32_t id;

    union {
        struct {
            ast_expr_t *expr;
        } expr;

        struct {
            ast_expr_t *condition;
            ast_stmt_t *then_stmt;
            ast_stmt_t *else_stmt;
        } if_stmt;

        struct {
            token_t label;

            /* Sema filled */
            ast_stmt_t *target;
        } goto_stmt;

        struct {
            token_t label;
            ast_stmt_t *stmt;
        } label_stmt;

        struct {
            ast_stmt_t *init;
            ast_expr_t *condition;
            ast_expr_t *post;
            ast_stmt_t *body;
        } loop;

        struct {
            ast_expr_t *condition;
            ast_stmt_t *body;

            /* Sema filled */
            ast_stmt_t *cases;
            ast_stmt_t *default_case;
        } switch_stmt;

        struct {
            ast_expr_t *expr;
            ast_stmt_t *stmt;

            /* Sema filled */
            int64_t value;
            ast_stmt_t *next_case;
        } case_stmt;

        struct {
            ast_stmt_t *stmt;
        } default_stmt;

        struct {
            /* Sema filled */
            ast_stmt_t *target;
        } break_stmt;

        struct {
            /* Sema filled */
            ast_stmt_t *target;
        } continue_stmt;

        struct {
            ast_expr_t *expr;
        } return_stmt;

        struct {
            LIST(ast_decl_t) decls;
        } decl;

        struct {
            LIST(ast_stmt_t) items;
        } block;
    };
};

/*
 * Translation unit
 */

struct ast_program_t {
    LIST(ast_decl_t) decls;
};

ast_program_t *ast_program_new(void);
ast_expr_t *ast_expr_new(expr_kind kind, token_t tok);
ast_stmt_t *ast_stmt_new(stmt_kind kind, token_t tok);
ast_decl_t *ast_decl_new(decl_kind kind, token_t tok);

#endif
