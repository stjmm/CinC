#include "sema.h"
#include "ast.h"
#include "type.h"
#include "diagnostics.h"
#include "base/list.h"
#include "base/hashmap.h"
#include "base/vector.h"
#include "base/memory.h"

#include <stdlib.h>
#include <stdarg.h>

typedef struct scope_t {
    struct scope_t *parent;
    hashmap ordinary;
} scope_t;

typedef struct {
    ast_decl_t *decl; // Current function
    type_t *return_ty;     

    ast_stmt_t *loop; // Continue target
    ast_stmt_t *breakable; // Break target
    ast_stmt_t *sw; // Owner of case/default
    ast_stmt_t *last_case; // Last case of switch

    hashmap labels; // name -> STMT_LABEL
    vector gotos;   // ast_stmt_t *
} function_ctx_t;

typedef struct {
    sema_result_t *result;

    scope_t *scope;
    hashmap linked_symbols;
    function_ctx_t fn_ctx;

    uint32_t next_symbol_id;
} sema_t;

static sema_t sema;

static void
error(token_t *tok, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    diagnostics_error(tok, fmt, args);
    va_end(args);
}

static scope_t *
scope_push(scope_t *parent)
{
    scope_t *scope = xmalloc(sizeof(scope_t));
    scope->parent = parent;
    hashmap_init(&scope->ordinary);
    return scope;
}

static scope_t *
scope_pop(scope_t *scope)
{
    scope_t *parent = scope->parent;
    hashmap_free(&scope->ordinary);
    free(scope);
    return parent;
}

static symbol_t *
scope_lookup(
    scope_t *scope,
    const char *name,
    size_t length)
{
    for (scope_t *sc = scope; sc; sc = sc->parent) {
        symbol_t *symbol = hashmap_get(
            &sc->ordinary,
            name,
            length);

        if (symbol)
            return symbol;
    }

    return nullptr;
}

static symbol_t *
scope_lookup_current(
    scope_t *scope,
    const char *name,
    size_t length)
{
    return hashmap_get(&scope->ordinary, name, length);
}

static bool
is_global_scope(scope_t *scope)
{
    return scope->parent == nullptr;
}

static symbol_t *
symbol_new(ast_decl_t *decl)
{
    symbol_t *symbol = xmalloc(sizeof(symbol_t));
    symbol->kind = decl->kind == DECL_FUNCTION
        ? SYMBOL_FUNCTION : SYMBOL_OBJECT;
    symbol->name = decl->name;

    return symbol;
}

/*
 * Expression analysis
 */

static void
convert_to_type(ast_expr_t **slot, type_t *target)
{
    ast_expr_t *expr = *slot;

    if (type_compatible(expr->ty, target))
        return;

    ast_expr_t *next = expr->next;
    expr->next = nullptr;

    ast_expr_t *cast = ast_expr_new(EXPR_CAST, expr->tok);
    cast->ty = target;
    cast->cast.target_ty = target;
    cast->cast.operand = expr;
    cast->is_lvalue = false;

    cast->next = next;
    *slot = cast;
}

static void
analyze_expr(ast_expr_t *expr)
{
    if (!expr)
        return;

    switch (expr->kind) {
        case EXPR_INT_CONSTANT:
            expr->ty = type_int();
            expr->is_lvalue = false;
            break;
        case EXPR_LONG_CONSTANT:
            expr->ty = type_long();
            expr->is_lvalue = false;
            break;
        case EXPR_IDENTIFIER:
            symbol_t *sym = scope_lookup(
                    sema.scope,
                    expr->tok.start,
                    expr->tok.len);

            if (!sym) {
                error(&expr->tok, "Undeclared identifier");
                expr->ty = type_int();
                expr->is_lvalue = false;
                break;
            }

            expr->identifier.sym = sym;
            expr->ty = sym->ty;
            expr->is_lvalue = sym->kind == SYMBOL_OBJECT;
            break;
        case EXPR_UNARY:
            analyze_expr(expr->unary.operand);

            expr->ty = expr->unary.op.kind == TOKEN_BANG
                ? type_int()
                : expr->unary.operand->ty;

            expr->is_lvalue = false;
            break;
        case EXPR_PRE:
        case EXPR_POST:
            analyze_expr(expr->unary.operand);

            if (!expr->unary.operand->is_lvalue)
                error(&expr->tok,
                        "Operand of increment/decrement must be an lvalue");

            expr->ty = expr->unary.operand->ty;
            expr->is_lvalue = false;
            break;
        case EXPR_BINARY:
            //...
        case EXPR_ASSIGNMENT:
            analyze_expr(expr->assignment.lvalue);
            analyze_expr(expr->assignment.rvalue);

            if (!expr->assignment.lvalue->is_lvalue)
                error(&expr->tok,
                        "Left side is not assignable");

            type_t *lvalue_type = expr->assignment.lvalue->ty;
            convert_to_type(&expr->assignment.rvalue, lvalue_type) ;

            expr->ty = lvalue_type;
            expr->is_lvalue = false;
            break;
        case EXPR_CONDITIONAL:
            //...
        case EXPR_CALL:
            analyze_expr(expr->call.callee);

            ast_expr_t *arg;
            LIST_FOREACH(arg, &expr->call.args) {
                analyze_expr(arg);
            }

            if (!type_is_function(expr->call.callee->ty)) {
                error(&expr->tok, "Called object is not a function");
                expr->ty = type_int();
                expr->is_lvalue = false;
                break;
            }
            //...
            break;
        case EXPR_CAST:
            analyze_expr(expr->cast.operand);
            expr->ty = expr->cast.target_ty;
            expr->is_lvalue = false;
            break;
    }
}

/*
 * Statement alaysis
 */

static void
analyze_stmt(ast_stmt_t *stmt)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_NULL:
            break;
        case STMT_EXPR:
            analyze_expr(stmt->expr.expr);
            break;
        case STMT_DECL:

        case STMT_RETURN:
            type_t *return_ty = sema.fn_ctx.decl->ty;
            ast_expr_t *val = stmt->return_stmt.expr;
            
            if (val)
                analyze_expr(val);

            if (type_is_void(return_ty)) {
                if (val)
                    error(&stmt->tok,
                            "Void functions should not return a value");
            } else if (!val) {
                error(&stmt->tok,
                        "Non-void functions must return a value");
            } else {
                convert_to_type(&val, return_ty);
            }
            break;
        case STMT_IF:
            analyze_expr(stmt->if_stmt.condition);
            analyze_stmt(stmt->if_stmt.then_stmt);
            analyze_stmt(stmt->if_stmt.else_stmt);
            break;
        case STMT_FOR:
            // New scope

            if (stmt->loop.init->kind == STMT_DECL) {

            }
            
            // Pop scope
    }
}

static void
analyze_block(ast_stmt_t *block, bool new_scope)
{
    if (new_scope)
        sema.scope = scope_push(sema.scope);

    ast_stmt_t *stmt;
    LIST_FOREACH(stmt, &block->block.items) {
        if (stmt->kind == STMT_DECL)
            analyze_declaration(stmt);
        else
            analyze_stmt(stmt);
    }

    if (new_scope)
        sema.scope = scope_pop(sema.scope);
}

/*
 * Declaration analysis
 */

static void
analyze_function(ast_decl_t *fn)
{
    sema.fn_ctx = (function_ctx_t){
        .decl = fn,
    };
    hashmap_init(&sema.fn_ctx.labels);
    VECTOR_INIT(&sema.fn_ctx.gotos, ast_stmt_t *);

    sema.scope = scope_push(nullptr);

    ast_decl_t *param;
    LIST_FOREACH(param, &fn->function.params) {
        validate_declaration(param);
        bind_declaration_symbol(param);
    }

    analyze_block(fn->function.body);
    resolve_gotos(fn->function.body);

    sema.scope = scope_pop(sema.scope);

    vector_free(&sema.fn_ctx.gotos);
    hashmap_free(&sema.fn_ctx.labels);
}

bool
sema_analyze(
    sema_result_t *result,
    ast_program_t *program)
{
    *result = (sema_result_t){
        .program = program
    };

    sema = (sema_t){
        .result = result
    };
    hashmap_init(&sema.linked_symbols);

    ast_decl_t *decl;
    LIST_FOREACH(decl, &program->decls) {
        validate_declaration(decl);
        bind_declaration_symbol(decl);

        if (decl->kind == DECL_OBJECT &&
                decl->object.init) {
            analyze_object_initializer(decl);
        }

        if (decl->kind == DECL_FUNCTION &&
                decl->function.body) {
            analyze_function(decl);
        }
    }

    return !diagnostics_had_error();
}
