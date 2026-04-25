/*
 * Semantic analysis pass. Operates on the AST and runs multiple passes:
 *  1. resolve_labels - finds all goto labels, catches duplicates
 *  2. resolve_block - variable resolution, renames to unique for scopes
 *  3. resolve_loops - assigns unique to loop/switches, resolves break/continue targets
 *  4. resolve_switches - collects case antries per switch into switch_annotation
 */

#ifndef CINC_SEMA_H
#define CINC_SEMA_H

#include <stdbool.h>

#include "lexer.h"

// Ordered linked-list of case entries collected from switch body
struct case_entry {
    struct ast_node *node; // Holds AST_CASE node
    struct case_entry *next;
};

/* Attached to each AST_SWITCH by after sema
* Contains all cases in source order and the default branch if present */
struct switch_annotation {
    struct case_entry *cases;
    struct ast_node *default_node;
};

// C11 6.7.1
enum storage_class {
    SC_NONE,
    SC_EXTERN,
    SC_STATIC,
    SC_AUTO,
};

// C11 6.2.2 linkages of identifiers
enum linkage {
    LINK_NONE,
    LINK_INTERNAL,
    LINK_EXTERNAL
};

// C11 6.2.4 storage duration of objects
enum storage_duration {
    SD_NONE,
    SD_AUTO,
    SD_STATIC,
};

enum symbol_kind {
    SYM_OBJECT,
    SYM_FUNCTION,
    SYM_PARAMETER
};

struct symbol {
    struct token name;

    enum symbol_kind kind;
    struct type *type;

    enum storage_class storage_class;
    enum linkage linkage;
    enum storage_duration storage_duration;

    /*
     * int x;        // tentative definition at file scope
     * int x = 1;    // definition
     * extern int x; // declaration, not definition
     */
    bool is_defined;
    bool is_tentative;

    // Name used by IR
    char *ir_name;

    struct ast_node *decl;
};

struct ast_node *sema_analysis(struct ast_node *program);

#endif
