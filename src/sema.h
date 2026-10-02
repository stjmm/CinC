#ifndef CINC_SEMA_H
#define CINC_SEMA_H

#include "ast.h"

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

    bool defined;
    bool tentative_definition;

    bool has_static_initializer;
    int64_t static_initializer;

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
