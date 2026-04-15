#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "sema.h"
#include "ast.h"

struct symbol {
    const char *name;
    int length;
    char *unique_name;
};

struct scope {
    struct scope *parent;
    struct symbol symbols[128];
    int count;
};

static int unique_counter = 0;
static bool had_error = false;

static struct scope *scope_push(struct scope *parent)
{
    struct scope *s = calloc(1, sizeof(struct scope));
    s->parent = parent;
    return s;
}

static struct scope *scope_pop(struct scope *s)
{
    struct scope *parent = s->parent;
    free(s);
    return parent;
}

static struct symbol *scope_find_local(struct scope *s, const char *name, int length)
{
    for (int i = 0; i < s->count; i++) {
        struct symbol *sym = &s->symbols[i];
        if (sym->length == length && memcmp(sym->name, name, length) == 0)
            return sym;
    }

    return NULL;
}

static struct symbol *scope_resolve(struct scope *s, const char *name, int length)
{
    for (struct scope *scp = s; scp != NULL; scp = scp->parent) {
        struct symbol *sym = scope_find_local(scp, name, length);
        if (sym)
            return sym;
    }

    return NULL;
}

// Returns 0 on succes, -1 if already exists
static int scope_declare(struct scope *s, const char *name, int length, char *unique_name)
{
    if (scope_find_local(s, name, length))
        return -1;

    s->symbols[s->count++] = (struct symbol) {
        .name = name,
        .length = length,
        .unique_name = unique_name,
    };

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
        case AST_BLOCK:
            return resolve_block(stmt, s);
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

static struct ast_node *resolve_function(struct ast_node *fn)
{
    resolve_block(fn->function.body, NULL);
    return fn;
}

struct ast_node *sema_analysis(struct ast_node *program)
{
    for (struct ast_node *fn = program->program.first; fn != NULL; fn = fn->next) {
        resolve_function(fn);
    }

    return had_error ? NULL : program;
}
