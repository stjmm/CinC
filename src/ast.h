#ifndef CINC_AST_H
#define CINC_AST_H

#include <stdlib.h>
#include <stdbool.h>

#include "lexer.h"

/*
 * AST is split into:
 *   - struct program: translation unit
 *   - struct decl:    object/function declaration
 *   - stmt and expr
 *
 * Sema annotates:
 *   - expr->ty
 *   - expr->is_lvalue
 *   - expr identifiers with expr->ident.sym
 *   - decl->sym
 *   - decl->ir_name
 *   - break/continue/switch/case labels
 */

#define LIST_APPEND(head, tail, node)  \
    do {                               \
        if (!(head))                   \
            (head) = (node);           \
        else                           \
            (tail)->next = (node);     \
        (tail) = (node);               \
    } while(0)


enum expr_kind {
    EXPR_INT_LITERAL,
    EXPR_IDENTIFIER,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_PRE,
    EXPR_POST,
    EXPR_ASSIGNMENT,
    EXPR_CONDITIONAL,
    EXPR_CALL
};

struct expr {
    enum expr_kind kind;
    struct token tok;
    struct expr *next; // Arguments list and other expression lists

    // Filled by sema
    struct type *type;
    bool is_lvalue;

    union {
        long int_value;

        struct {
            struct token name;
            struct symbol *sym;
        } identifier;

        struct {
            struct token op;
            struct expr *operand;
        } unary;

        struct {
            struct token op;
            struct expr *left;
            struct expr *right;
        } binary;

        struct {
            struct token op;
            struct expr *lvalue;
            struct expr *rvalue;
        } assignment;

        struct {
            struct expr *condition;
            struct expr *then_expr;
            struct expr *else_expr;
        } conditional;

        struct {
            struct expr *callee;
            struct expr *args;
        } call;
    };
};

enum decl_kind {
    DECL_OBJECT,
    DECL_FUNCTION
};

enum storage_class {
    SC_NONE,
    SC_EXTERN,
    SC_STATIC,
    SC_AUTO,
    SC_REGISTER,
};

enum linkage {
    LINK_NONE,
    LINK_INTERNAL,
    LINK_EXTERNAL
};

enum storage_duration {
    SD_NONE,
    SD_AUTO,
    SD_STATIC,
    SD_THREAD
};

struct decl {
    enum decl_kind kind;
    struct token name;
    struct decl *next;

    struct type *type;

    enum storage_class storage_class;        // parsed
    enum linkage linkage;                    // sema-computed
    enum storage_duration storage_duration;  // sema-computed

    bool is_definition;
    bool is_tentative;
    bool is_parameter;

    struct symbol *sym;
    char *ir_name;

    union {
        struct {
            struct expr *init;
        } object;

        struct {
            struct decl *params;
            struct stmt *body;
        } func;
    };
};

/* Statements / block-items */

enum block_item_kind {
    BLOCK_ITEM_STMT,
    BLOCK_ITEM_DECL
};

struct block_item {
    enum block_item_kind kind;
    struct block_item *next;
    struct token tok;

    union {
        struct stmt *stmt;
        struct decl *decls;
    };
};

struct for_init {
    bool is_decl;
    union {
        struct expr *expr;
        struct decl *decls;
    };
};

enum stmt_kind {
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
};

struct stmt {
    enum stmt_kind kind;
    struct token tok;
    struct stmt *next;

    union {
        struct {
            struct expr *expr;
        } expr_stmt;

        struct {
            struct expr *condition;
            struct stmt *then_stmt;
            struct stmt *else_stmt;
        } if_stmt;

        struct {
            struct token label;
        } goto_stmt;

        struct {
            struct token name;
            struct stmt *stmt;
        } label_stmt;

        struct {
            const char *target_label;
        } break_stmt;

        struct {
            const char *target_label;
        } continue_stmt;

        struct {
            struct for_init *init;
            struct expr *condition;
            struct expr *post;
            struct stmt *body;
            const char *break_label;
            const char *continue_label;
        } for_stmt;

        struct {
            struct expr *condition;
            struct stmt *body;
            const char *break_label;
            const char *continue_label;
        } while_stmt;

        struct {
            struct stmt *body;
            struct expr *condition;
            const char *break_label;
            const char *continue_label;
        } dowhile_stmt;

        struct {
            struct stmt *body;
            struct expr *condition;
            const char *break_label;
            struct switch_annotation *annotation;
        } switch_stmt;

        struct {
            struct expr *value;
            struct block_item *items;
            const char *label;
        } case_stmt;

        struct {
            struct block_item *items;
            const char *label;
        } default_stmt;

        struct {
            struct expr *expr;
        } return_stmt;

        struct {
            struct block_item *items;
        } block;
    };
};

/* Translation Unit */

struct ast_program {
    struct decl *decls;
};

static inline struct expr *expr_new(enum expr_kind kind, struct token tok)
{
    struct expr *e = calloc(1, sizeof(struct expr));
    e->kind = kind;
    e->tok = tok;
    return e;
}

static inline struct stmt *stmt_new(enum stmt_kind kind, struct token tok)
{
    struct stmt *s = calloc(1, sizeof(struct stmt));
    s->kind = kind;
    s->tok = tok;
    return s;
}

static inline struct decl *decl_new(enum decl_kind kind, struct token tok)
{
    struct decl *d = calloc(1, sizeof(struct decl));
    d->kind = kind;
    d->name = tok;
    d->storage_class = SC_NONE;
    d->storage_duration = SD_NONE;
    d->linkage = LINK_NONE;
    return d;
}

static inline struct block_item *block_item_new(enum block_item_kind kind, struct token tok)
{
    struct block_item *i = calloc(1, sizeof(struct block_item));
    i->kind = kind;
    i->tok = tok;
    return i;
}

void ast_print(struct ast_program *program);

#endif
