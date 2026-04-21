/*
 * Semantic analysis pass. Operates on the AST and runs multiple passes:
 *  1. resolve_labels - finds all goto labels, catches duplicates
 *  2. resolve_block - variable resolution, renames to unique for scopes
 *  3. resolve_loops - assigns unique to loop/switches, resolves break/continue targets
 *  4. resolve_switches - collects case antries per switch into switch_annotation
 */

#ifndef CINC_SEMA_H
#define CINC_SEMA_H

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

struct ast_node *sema_analysis(struct ast_node *program);

#endif
