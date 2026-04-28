#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "sema.h"
#include "ast.h"
#include "base/hash_map.h"

struct scope {
    struct scope *parent;
    hash_map ordinary;
};

struct label_symbol {
    struct token name;
    struct stmt *stmt;
};

struct sema {
    struct scope *global_scope;
    struct scope *current_scope;

    hash_map labels;

    struct decl *current_function;

    int unqiue_counter;
    bool had_error;
};

struct switch_ctx {
    struct stmt *switch_stmt;
    struct switch_annotation *annotation;
};

struct control_ctx {
    const char *break_label;
    const char *continue_label;
    struct switch_ctx *switch_ctx;
};

static struct sema sema_state;

static struct scope *scope_push(struct scope *parent)
{
    struct scope *s = calloc(1, sizeof(struct scope));
    hashmap_init(&s->ordinary);
    s->parent = parent;
    return s;
}

static struct scope *scope_pop(struct scope *s)
{
    struct scope *parent = s->parent;
    hashmap_free(&s->ordinary);
    free(s);
    return parent;
}

static struct symbol *scope_resolve_current(struct scope *s, const char *name, int length)
{
    if (!s)
        return NULL;

    return hashmap_get(&s->ordinary, name, length);
}

// Walks all scopes from innermost to outermost looking for 'name'
static struct symbol *scope_resolve(struct scope *s, const char *name, int length)
{
    for (struct scope *scp = s; scp != NULL; scp = scp->parent) {
        struct symbol *sym = hashmap_get(&scp->ordinary, name, length);
        if (sym)
            return sym;
    }

    return NULL;
}

static struct symbol *symbol_new(struct token name, struct decl *decl)
{
    struct symbol *sym = calloc(1, sizeof(struct symbol));
    sym->name = name;
    sym->ns = NS_ORDINARY;
    sym->decl = decl;
    sym->linkage = decl->linkage;
    sym->ir_name = decl->ir_name;
    sym->ir_name_len = decl->ir_name_len;
    return sym;
}

/* Declares a scope
* Returns 0 on succes, -1 if already exists in scope */
static int scope_declare(struct scope *s, const char *name, int length, char *unique_name)
{
    if (hashmap_get(&s->symbols, name, length))
        return -1;

    struct symbol *sym = malloc(sizeof(struct symbol));
    *sym = (struct symbol) {
        .name = name,
        .length = length,
        .unique_name = unique_name,
    };

    hashmap_set(&s->symbols, name, length, sym);
    return 0;
}

/* Renames 'name' to a globally unique string in form name.N
*  Used to differentaiate variables with the same name in different scopes */
static char *make_unique(const char *name, int length)
{
    int n = snprintf(NULL, 0, "%.*s.%d", length, name, unique_counter);
    char *buf = malloc(n + 1);
    snprintf(buf, n + 1, "%.*s.%d", length, name, unique_counter++);

    return buf;
}

static void error(struct token *tok, const char *message)
{
    int col = (int)(tok->start - tok->line_start);

    fprintf(stderr, "Error at line %d, col %d: %s\n", tok->line, col, message);

    const char *line_end = tok->line_start;
    while (*line_end != '\0' && *line_end != '\n')
        line_end++;
    fprintf(stderr, "  %.*s\n", (int)(line_end - tok->line_start), tok->line_start);

    fprintf(stderr, "  %*s", col, "");
    for (size_t i = 0; i < (tok->length > 0 ? tok->length : 1); i++)
        fputc('^', stderr);
    fputc('\n', stderr);

    had_error = true;
}

/* Variable Resolution */

static struct ast_node *resolve_block(struct ast_node *block, struct scope *parent);
static struct ast_node *resolve_statement(struct ast_node *stmt, struct scope *s);

static struct ast_node *resolve_expr(struct ast_node *expr, struct scope *s)
{
    if (!expr)
        return NULL;

    switch (expr->type) {
        case AST_IDENTIFIER: {
            struct symbol *sym = scope_resolve(s, expr->token.start, expr->token.length);
            if (!sym) {
                error(&expr->token, "Undeclared variable");
                return NULL;
            }

            expr->token.resolved = sym->unique_name;
            expr->token.resolved_length = strlen(sym->unique_name);

            return expr;
        }
        case AST_CALL: {
            struct symbol *sym = scope_resolve(s, expr->call.name.start, expr->call.name.length);
            if (!sym) {
                error(&expr->call.name, "Undeclared function");
                return NULL;
            }

            expr->call.name.resolved = sym->unique_name;
            expr->call.name.resolved_length = strlen(sym->unique_name);

            for (struct ast_node *arg = expr->call.args; arg; arg = arg->next)
                arg = resolve_expr(arg, s);

            return expr;
        }
        case AST_ASSIGNMENT: {
            if (expr->assignment.lvalue->type != AST_IDENTIFIER) {
                error(&expr->token, "Invalid lvalue in assignment");
                return NULL;
            }

            expr->assignment.lvalue = resolve_expr(expr->assignment.lvalue, s);
            expr->assignment.rvalue = resolve_expr(expr->assignment.rvalue, s);
            return expr;
        }
        case AST_PRE:
        case AST_POST: {
            if (expr->unary.expr->type != AST_IDENTIFIER) {
                error(&expr->unary.expr->token, "Operand of '++'/'--' must be an lvalue");
                return NULL;
            }

            expr->unary.expr = resolve_expr(expr->unary.expr, s);
            return expr;

        }
        case AST_TERNARY:
            expr->ternary.condition = resolve_expr(expr->ternary.condition, s);
            expr->ternary.then = resolve_expr(expr->ternary.then, s);
            expr->ternary.else_then = resolve_expr(expr->ternary.else_then, s);
            return expr;
        case AST_CONSTANT:
            return expr;
        case AST_BINARY:
            expr->binary.left = resolve_expr(expr->binary.left, s);
            expr->binary.right = resolve_expr(expr->binary.right, s);
            return expr;
        case AST_UNARY:
            expr->unary.expr = resolve_expr(expr->unary.expr, s);
            return expr;
        default:
            return NULL;
    }
}

static struct ast_node *resolve_declaration(struct ast_node *decl, struct scope *s)
{
    struct token *tok = &decl->var_decl.name;
    char *unique = make_unique(tok->start, tok->length);
    
    if (scope_declare(s, tok->start, tok->length, unique) < 0) {
        error(tok, "Duplicate variable declaration");
        free(unique);
        return NULL;
    }

    if (decl->var_decl.init)
        decl->var_decl.init = resolve_expr(decl->var_decl.init, s);

    tok->resolved = unique;
    tok->resolved_length = strlen(unique);

    return decl;
}

static struct ast_node *resolve_statement(struct ast_node *stmt, struct scope *s)
{
    if (!stmt)
        return NULL;

    switch (stmt->type) {
        case AST_NULL_STMT:
        case AST_BREAK:
        case AST_CONTINUE:
            break;
        case AST_GOTO: {
            struct token *tok = &stmt->goto_stmt.label;
            if (!hashmap_get(&labels, tok->start, tok->length))
                error(tok, "Use of undeclared label");
            break;
        }
        case AST_RETURN:
            stmt->return_stmt.expr = resolve_expr(stmt->return_stmt.expr, s);
            break;
        case AST_EXPR_STMT:
            stmt->expr_stmt.expr = resolve_expr(stmt->expr_stmt.expr, s);
            break;
        case AST_IF_STMT:
            stmt->if_stmt.condition = resolve_expr(stmt->if_stmt.condition, s);
            stmt->if_stmt.then = resolve_statement(stmt->if_stmt.then, s);
            if (stmt->if_stmt.else_then)
                stmt->if_stmt.else_then = resolve_statement(stmt->if_stmt.else_then, s);
            break;
        case AST_FOR: {
            // for_stmt gets its own scope so 'for (int i...)' i doesn't leak into enlosing loop 
            struct scope *new_scope = scope_push(s);
            struct ast_node *for_init = stmt->for_stmt.for_init;
            if (for_init) {
                if (for_init->type == AST_VAR_DECL)
                    resolve_declaration(for_init, new_scope);
                else
                    resolve_expr(for_init, new_scope);
            }

            stmt->for_stmt.condition = resolve_expr(stmt->for_stmt.condition, new_scope);
            stmt->for_stmt.post = resolve_expr(stmt->for_stmt.post, new_scope);
            stmt->for_stmt.body = resolve_statement(stmt->for_stmt.body, new_scope);
            scope_pop(new_scope);
            break;
        }
        case AST_WHILE: {
            stmt->while_stmt.condition = resolve_expr(stmt->while_stmt.condition, s);
            stmt->while_stmt.body = resolve_statement(stmt->while_stmt.body, s);
            break;
        }
        case AST_DOWHILE: {
            stmt->do_while.body = resolve_statement(stmt->do_while.body, s);
            stmt->do_while.condition = resolve_expr(stmt->do_while.condition, s);
            break;
        }
        case AST_SWITCH:
            stmt->switch_stmt.condition = resolve_expr(stmt->switch_stmt.condition, s);
            resolve_statement(stmt->switch_stmt.body, s);
            break;
        case AST_CASE:
            for (struct ast_node *item = stmt->case_stmt.first; item; item = item->next)
                resolve_statement(item, s);
            break;
        case AST_DEFAULT:
            for (struct ast_node *item = stmt->default_stmt.first; item; item = item->next)
                resolve_statement(item, s);
            break;
        case AST_BLOCK:
            return resolve_block(stmt, s);
        case AST_LABEL_STMT:
            resolve_statement(stmt->label_stmt.stmt, s);
            break;
        default:
            break;
    }

    return stmt;
}

static struct ast_node *resolve_block(struct ast_node *block, struct scope *parent)
{
    struct scope *s = scope_push(parent);

    for (struct ast_node *item = block->block.first; item != NULL; item = item->next) {
        if (item->type == AST_VAR_DECL)
            resolve_declaration(item, s);
        else
            resolve_statement(item, s);
    }

    scope_pop(s);
    return block;
}

// Collects all goto labels in a function so forward gotos work
static void resolve_labels(struct ast_node *node)
{
    if (!node)
        return;

    switch (node->type) {
        case AST_LABEL_STMT: {
            struct token *tok = &node->label_stmt.name;
            if (hashmap_get(&labels, tok->start, tok->length))
                error(tok, "Duplicate label defnintion");
            else
                hashmap_set(&labels, tok->start, tok->length, node);

            // C23 allows this, C11 doesn't
            if (node->label_stmt.stmt != NULL && node->label_stmt.stmt->type == AST_VAR_DECL)
                error(tok, "Label cannot be followed by a declaration");

            // Label has a statement, so we need to resolve it
            resolve_labels(node->label_stmt.stmt);
            break;
        }
        case AST_IF_STMT: {
            resolve_labels(node->if_stmt.then);
            resolve_labels(node->if_stmt.else_then);
            break;
        }
        case AST_BLOCK: {
            for (struct ast_node *item = node->block.first; item; item = item->next)
                resolve_labels(item);
            break;
        }
        case AST_FOR:
            resolve_labels(node->for_stmt.body);
            break;
        case AST_WHILE:
            resolve_labels(node->while_stmt.body);
            break;
        case AST_DOWHILE:
            resolve_labels(node->do_while.body);
            break;
        case AST_SWITCH:
            resolve_labels(node->switch_stmt.body);
            break;
        case AST_CASE:
            for (struct ast_node *item = node->case_stmt.first; item; item = item->next)
                resolve_labels(item);
            break;
        case AST_DEFAULT:
            for (struct ast_node *item = node->default_stmt.first; item; item = item->next)
                resolve_labels(item);
            break;
        default:
            break;
    }
}

/* Label loops and their break and continues
 * Error if break or continue outside of a loop */
static void resolve_break_continue(struct ast_node *stmt, struct loop_ctx *ctx)
{
    if (!stmt)
        return;

    switch(stmt->type) {
        case AST_BREAK: {
            if (!ctx || !ctx->break_label)
                error(&stmt->token, "'break' statement outside of loop or switch");
            else
                stmt->break_stmt.target_label = ctx->break_label;
            break;
        }
        case AST_CONTINUE: {
            if (!ctx || !ctx->continue_label)
                error(&stmt->token, "'continue' statement outside of loop");
            else
                stmt->continue_stmt.target_label = ctx->continue_label;
            break;
        }
        case AST_WHILE: {
            char *lbl = make_unique("while", 5);
            stmt->while_stmt.label = lbl;
            struct loop_ctx new_ctx = { .break_label = lbl, .continue_label = lbl };
            resolve_break_continue(stmt->while_stmt.body, &new_ctx);
            break;
        }
        case AST_DOWHILE: {
            char *lbl = make_unique("dowhile", 7);
            stmt->do_while.label = lbl;
            struct loop_ctx new_ctx = { .break_label = lbl, .continue_label = lbl };
            resolve_break_continue(stmt->do_while.body, &new_ctx);
            break;
        }
        case AST_FOR: {
            char *lbl = make_unique("for", 3);
            stmt->for_stmt.label = lbl;
            struct loop_ctx new_ctx = { .break_label = lbl, .continue_label = lbl };
            resolve_break_continue(stmt->for_stmt.body, &new_ctx);
            break;
        }

        case AST_IF_STMT: {
            resolve_break_continue(stmt->if_stmt.then, ctx);
            resolve_break_continue(stmt->if_stmt.else_then, ctx);
            break;
        }
        case AST_SWITCH: {
            char *lbl = make_unique("switch", 6);
            stmt->switch_stmt.label = lbl;
            // switch break targets the switch, switch continue targets enclosing loop
            struct loop_ctx new_ctx = { 
                .break_label = lbl,
                .continue_label = ctx ? ctx->continue_label : NULL,
            };
            resolve_break_continue(stmt->switch_stmt.body, &new_ctx);
            break;
        }
        case AST_CASE: {
            for (struct ast_node *item = stmt->case_stmt.first; item; item = item->next)
                resolve_break_continue(item, ctx);
            break;
        }
        case AST_DEFAULT: {
            for (struct ast_node *item = stmt->default_stmt.first; item; item = item->next)
                resolve_break_continue(item, ctx);
            break;
        }
        case AST_BLOCK: {
            for (struct ast_node *item = stmt->block.first; item; item = item->next)
                resolve_break_continue(item, ctx);
            break;
        }
        case AST_LABEL_STMT:
            resolve_break_continue(stmt->label_stmt.stmt, ctx);
            break;
        default:
            break;
    }
}

static void resolve_cases(struct ast_node *node, struct switch_annotation *ann)
{
    if (!node)
        return;

    switch(node->type) {
        case AST_CASE: {
            if (node->case_stmt.value->type != AST_CONSTANT) {
                error(&node->token, "'case' value must be a constant integer");
                return;
            }

            // Check for duplicate case values in this switch
            long val = node->case_stmt.value->constant.value;
            for (struct case_entry *e = ann->cases; e; e = e->next) {
                if (e->node->case_stmt.value->constant.value == val) {
                    error(&e->node->token, "Duplicate case value in switch");
                    return;
                }
            }

            node->case_stmt.label = make_unique("case", 4);
            struct case_entry *entry = malloc(sizeof(struct case_entry));
            *entry = (struct case_entry){ .node = node, .next = NULL };

            // Append to preserve order in switch
            struct case_entry **tail = &ann->cases;
            while (*tail)
                tail = &(*tail)->next;
            *tail = entry;

            // Descend into case statements (may contains blocks etc)
            for (struct ast_node *item = node->case_stmt.first; item; item = item->next)
                resolve_cases(item, ann);
            break;
        }
        case AST_DEFAULT: {
            if (ann->default_node) {
                error(&node->token, "Multiple 'default' labels in one switch");
                return;
            }

            node->default_stmt.label = make_unique("default", 7);
            ann->default_node = node;

            struct case_entry *entry = malloc(sizeof(struct case_entry));
            *entry = (struct case_entry) { .node = node, .next = NULL };
            struct case_entry **tail = &ann->cases;
            while (*tail)
                tail = &(*tail)->next;
            *tail = entry;

            // Descend into default statements
            for (struct ast_node *item = node->default_stmt.first; item; item = item->next)
                resolve_cases(item, ann);
            break;
        }
        // Stop descending at nested switches - they own their own case list
        case AST_SWITCH:
            break;
        case AST_BLOCK:
            for (struct ast_node *item = node->block.first; item; item = item->next)
                resolve_cases(item, ann);
            break;
        case AST_IF_STMT:
            resolve_cases(node->if_stmt.then, ann);
            resolve_cases(node->if_stmt.else_then, ann);
            break;
        case AST_WHILE:
            resolve_cases(node->while_stmt.body, ann);
            break;
        case AST_FOR:
            resolve_cases(node->for_stmt.body, ann);
            break;
        case AST_DOWHILE:
            resolve_cases(node->do_while.body, ann);
            break;
        default:
            break;
    }
}

static void resolve_switches(struct ast_node *node)
{
    if (!node)
        return;

    switch (node->type) {
        case AST_SWITCH: {
            struct switch_annotation *ann = calloc(1, sizeof(struct switch_annotation));

            resolve_cases(node->switch_stmt.body, ann);
            node->switch_stmt.annotation = ann;

            // Nested switches
            resolve_switches(node->switch_stmt.body);
            break;
        }
        case AST_CASE:
            for (struct ast_node *item = node->case_stmt.first; item; item = item->next)
                resolve_switches(item);
            break;
        case AST_DEFAULT:
            for (struct ast_node *item = node->default_stmt.first; item; item = item->next)
                resolve_switches(item);
            break;
        case AST_BLOCK:
            for (struct ast_node *item = node->block.first; item; item = item->next)
                resolve_switches(item);
            break;
        case AST_IF_STMT:
            resolve_switches(node->if_stmt.then);
            resolve_switches(node->if_stmt.else_then);
            break;
        case AST_WHILE:
            resolve_switches(node->while_stmt.body);
            break;
        case AST_FOR:
            resolve_switches(node->for_stmt.body);
            break;
        case AST_DOWHILE:
            resolve_switches(node->do_while.body);
            break;
        case AST_LABEL_STMT:
            resolve_switches(node->label_stmt.stmt);
            break;
        default:
            break;
    }
}

static void resolve_function(struct decl *fn)
{
    if (!fn->func.body)
        return NULL;

    sema_state.current_function = fn;

    hashmap_init(&ctx->labels);
    collect_labels(fn);

    return fn;
}

struct ast_program *sema_analysis(struct ast_program *program)
{
    sema_state = {0};

    sema_state.global_scope = scope_push(NULL);
    sema_state.current_scope = sema_state.global_scope;

    for (struct decl *decl = program->decls, decl; decl = decl->next) {
        sema_decl(decl, true);

        if (decl->kind == DECL_FUNC && decl->is_definition)
            sema_function_body(decl);
    }

    scope_pop(sema_state.global_scope);

    return sema_state.had_error ? NULL : program;
}
