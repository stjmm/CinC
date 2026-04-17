#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "sema.h"
#include "ast.h"
#include "base/hash_map.h"

struct symbol {
    const char *name;
    int length;
    char *unique_name;
};

struct scope {
    struct scope *parent;
    hash_map symbols;
};

struct loop_ctx {
    const char *label;
    struct loop_ctx *prev;
};

static hash_map labels;
static int unique_counter = 0;
static bool had_error = false;

static struct scope *scope_push(struct scope *parent)
{
    struct scope *s = calloc(1, sizeof(struct scope));
    hm_init(&s->symbols);
    s->parent = parent;
    return s;
}

static struct scope *scope_pop(struct scope *s)
{
    struct scope *parent = s->parent;
    hm_free(&s->symbols);
    free(s);
    return parent;
}

static struct symbol *scope_resolve(struct scope *s, const char *name, int length)
{
    for (struct scope *scp = s; scp != NULL; scp = scp->parent) {
        struct symbol *sym = hm_get(&scp->symbols, name, length);
        if (sym)
            return sym;
    }

    return NULL;
}

// Returns 0 on succes, -1 if already exists
static int scope_declare(struct scope *s, const char *name, int length, char *unique_name)
{
    if (hm_get(&s->symbols, name, length))
        return -1;

    struct symbol *sym = malloc(sizeof(struct symbol));
    *sym = (struct symbol) {
        .name = name,
        .length = length,
        .unique_name = unique_name,
    };

    hm_set(&s->symbols, name, length, sym);
    return 0;
}

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
    struct token *tok = &decl->declaration.name->token;
    char *unique = make_unique(tok->start, tok->length);
    
    if (scope_declare(s, tok->start, tok->length, unique) < 0) {
        error(tok, "Duplicate variable declaration");
        free(unique);
        return NULL;
    }

    if (decl->declaration.init)
        decl->declaration.init = resolve_expr(decl->declaration.init, s);

    tok->resolved = unique;
    tok->resolved_length = strlen(unique);

    return decl;
}

static struct ast_node *resolve_statement(struct ast_node *stmt, struct scope *s)
{
    if (!stmt)
        return NULL;

    switch (stmt->type) {
        case AST_RETURN:
            stmt->return_stmt.expr = resolve_expr(stmt->return_stmt.expr, s);
            break;
        case AST_EXPR_STMT:
            stmt->expr_stmt.expr = resolve_expr(stmt->expr_stmt.expr, s);
            break;
        case AST_NULL_STMT:
            return stmt;
            break;
        case AST_IF_STMT:
            stmt->if_stmt.condition = resolve_expr(stmt->if_stmt.condition, s);
            stmt->if_stmt.then = resolve_statement(stmt->if_stmt.then, s);
            if (stmt->if_stmt.else_then)
                stmt->if_stmt.else_then = resolve_statement(stmt->if_stmt.else_then, s);
            break;
        case AST_FOR: {
            struct scope *new_scope = scope_push(s);
            struct ast_node *for_init = stmt->for_stmt.for_init;
            if (for_init) {
                if (for_init->type == AST_DECLARATION)
                    for_init = resolve_declaration(for_init, new_scope);
                else
                    for_init = resolve_expr(for_init, new_scope);
            }

            struct ast_node *condition = stmt->for_stmt.condition;
            if (condition)
                condition = resolve_expr(condition, new_scope);

            struct ast_node *post = stmt->for_stmt.post;
            if (post)
                post = resolve_expr(post, new_scope);

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
        case AST_BLOCK:
            return resolve_block(stmt, s);
        case AST_GOTO: {
            struct token *tok = &stmt->goto_stmt.label;
            if (!hm_get(&labels, tok->start, tok->length))
                error(tok, "Use of undeclared label");
            break;
        }
        default:
            ;
    }

    return stmt;
}

static struct ast_node *resolve_block(struct ast_node *block, struct scope *parent)
{
    struct scope *s = scope_push(parent);

    for (struct ast_node *item = block->block.first; item != NULL; item = item->next) {
        if (item->type == AST_DECLARATION)
            resolve_declaration(item, s);
        else
            resolve_statement(item, s);
    }

    scope_pop(s);
    return block;
}

/* goto and labels resolution */

static void collect_labels(struct ast_node *node)
{
    if (!node)
        return;

    switch (node->type) {
        case AST_LABEL_STMT: {
            struct token *tok = &node->label_stmt.name;
            if (hm_get(&labels, tok->start, tok->length))
                error(tok, "Duplicate label defnintion");
            else
                hm_set(&labels, tok->start, tok->length, node);


            // TODO: Which standard?
            // C23 allows this...
            //
            // if (node->next == NULL)
            //     error(tok, "Label at the end of a block must be followed by a statement");
            if (node->next != NULL && node->next->type == AST_DECLARATION)
                error(tok, "Label cannot be followed by a declaration");
            break;
        }
        case AST_IF_STMT: {
            collect_labels(node->if_stmt.then);
            collect_labels(node->if_stmt.else_then);
            break;
        }
        case AST_BLOCK: {
            for (struct ast_node *item = node->block.first; item; item = item->next)
                collect_labels(item);
            break;
        }
        case AST_FOR:
            collect_labels(node->for_stmt.body);
            break;
        case AST_WHILE:
            collect_labels(node->while_stmt.body);
            break;
        case AST_DOWHILE:
            collect_labels(node->do_while.body);
            break;
        default:
            break;
    }
}

/* Loop labeling */

static void label_loops(struct ast_node *node, struct loop_ctx *ctx);

static void label_loops_block(struct ast_node *node, struct loop_ctx *ctx)
{
    for (struct ast_node *item = node->block.first; item != NULL; item = item->next) {
        label_loops(item, ctx);
    }
}

static void label_loops(struct ast_node *stmt, struct loop_ctx *ctx)
{
    if (!stmt)
        return;

    switch(stmt->type) {
        case AST_BREAK: {
            if (!ctx)
                error(&stmt->token, "'break' statement outside of loop or switch");
            else
                stmt->break_stmt.target_label = ctx->label;
            break;
        }
        case AST_CONTINUE: {
            if (!ctx)
                error(&stmt->token, "'continue' statement outside of loop or switch");
            else
                stmt->continue_stmt.target_label = ctx->label;
            break;
        }
        case AST_WHILE: {
            char *lbl = make_unique("while", 5);
            stmt->while_stmt.label = lbl;
            struct loop_ctx new_ctx = { .label = lbl, .prev = ctx };
            label_loops(stmt->while_stmt.body, &new_ctx);
            break;
        }
        case AST_DOWHILE: {
            char *lbl = make_unique("dowhile", 7);
            stmt->do_while.label = lbl;
            struct loop_ctx new_ctx = { .label = lbl, .prev = ctx };
            label_loops(stmt->do_while.body, &new_ctx);
            break;
        }
        case AST_FOR: {
            char *lbl = make_unique("for", 3);
            stmt->for_stmt.label = lbl;
            struct loop_ctx new_ctx = { .label = lbl, .prev = ctx };
            label_loops(stmt->for_stmt.body, &new_ctx);
            break;
        }
        case AST_IF_STMT: {
            label_loops(stmt->if_stmt.then, ctx);
            label_loops(stmt->if_stmt.else_then, ctx);
            break;
        }
        case AST_BLOCK: {
            label_loops_block(stmt, ctx);
            break;
        }
        default:
            break;
    }
}

static struct ast_node *resolve_function(struct ast_node *fn)
{
    hm_init(&labels);

    collect_labels(fn->function.body); // gotos, labels resolution
    resolve_block(fn->function.body, NULL); // variable resolution
    label_loops(fn->function.body, NULL);

    hm_free(&labels);
    return fn;
}

struct ast_node *sema_analysis(struct ast_node *program)
{
    for (struct ast_node *fn = program->program.first; fn != NULL; fn = fn->next) {
        resolve_function(fn);
    }

    return had_error ? NULL : program;
}
