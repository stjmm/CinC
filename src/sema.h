#ifndef CINC_SEMA_H
#define CINC_SEMA_H

#include <stdbool.h>

#include "ast.h"

enum symbol_namespace {
    NS_ORDINARY,
};

struct symbol {
    struct token name;
    enum symbol_namespace ns;

    struct decl *decl;

    enum linkage linkage;

    const char *ir_name;
    int ir_name_len;

    bool has_definiton;
    struct decl *definiton;

    struct decl *tentative_definition;
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
