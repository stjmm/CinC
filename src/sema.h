#ifndef CINC_SEMA_H
#define CINC_SEMA_H

#include "ast.h"

typedef enum {
    INIT_NONE,
    INIT_TENTATIVE,
    INIT_CONSTANT
} init_kind;

typedef enum {
    SYMBOL_OBJECT,
    SYMBOL_FUNCTION
} symbol_kind;

struct symbol_t {
    symbol_kind kind;

    token_t name;
    size_t id;

    type_t *ty;
    ast_decl_t *decl;

    linkage linkage;

    storage_duration sd;
    init_kind init;
    int64_t init_value;
    bool defined;

    symbol_t *next;
};

typedef struct {
    ast_program_t *program;
    LIST(symbol_t) symbols;
} sema_result_t;

bool sema_analyze(
    sema_result_t *result,
    ast_program_t *program);

#endif
