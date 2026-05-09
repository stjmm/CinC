#ifndef CINC_SEMA_H
#define CINC_SEMA_H

#include <stdbool.h>

#include "ast.h"

enum symbol_kind {
    SYM_OBJECT,
    SYM_FUNCTION
};

struct symbol {
    enum symbol_kind kind;

    const char *name;
    int name_len;

    struct type *ty;
    struct decl *decl;

    enum linkage linkage;
    enum storage_duration storage_duration;

    bool defined;
    bool tentative;

    bool has_static_init;
    long static_init;

    char *ir_name;

    struct symbol *next;
};

struct case_entry {
    struct stmt *node;
    struct case_entry *next;
};

struct switch_annotation {
    struct case_entry *cases;
    struct stmt *default_node;
};

struct ast_program *sema_analysis(struct ast_program *program);

#endif
