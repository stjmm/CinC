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

    ast_stmt_t *loop; // Current continue target
    ast_stmt_t *breakable; // Current break target
    ast_stmt_t *sw; // Current owner of case/default
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
    scope_t *scope = xcalloc(1, sizeof(scope_t));
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
is_global_scope(void)
{
    return sema.scope->parent == nullptr;
}

static symbol_t *
symbol_new(ast_decl_t *decl)
{
    symbol_t *symbol = xcalloc(1, sizeof(symbol_t));
    symbol->kind = decl->kind == DECL_FUNCTION
        ? SYMBOL_FUNCTION : SYMBOL_OBJECT;
    symbol->name = decl->name;
    symbol->id = sema.next_symbol_id++;
    symbol->decl= decl;
    symbol->ty = decl->ty;
    symbol->linkage = decl->link;
    symbol->sd = decl->sd;
    symbol->defined = decl->kind == DECL_FUNCTION && decl->is_definition;
    symbol->init = decl->is_tentative ? INIT_TENTATIVE : INIT_NONE;

    if (symbol->sd == STORAGE_DURATION_STATIC)
        LIST_APPEND(&sema.result->symbols, symbol);

    return symbol;
}

/*
 * Expression analysis
 */

static int64_t
constant_convert(int64_t value, type_t *ty)
{
    if (type_is_int(ty))
        return (int32_t)value;

    return value;
}

static bool
eval_constant(ast_expr_t *expr, int64_t *out_value)
{
    if (!expr)
        return false;

    switch (expr->kind) {
        case EXPR_INT_CONSTANT:
        case EXPR_LONG_CONSTANT:
            *out_value = expr->constant_value;
            return true;
        case EXPR_CAST: {
            int64_t value;

            if (!type_is_integer(expr->ty) ||
                    !eval_constant(expr->cast.operand, &value)) {
                return false;
            }

            *out_value = constant_convert(value, expr->ty);
            return true;
        }
        case EXPR_UNARY: {
            int64_t value;

            if (!eval_constant(expr->unary.operand, &value))
                return false;

            switch (expr->unary.op.kind) {
                case TOKEN_PLUS:
                    break;
                case TOKEN_MINUS:
                    value = (int64_t)(0 - (uint64_t)value);
                    break;
                case TOKEN_TILDE:
                    value = ~value;
                    break;
                case TOKEN_BANG:
                    value = !value;
                    break;
                default:
                    return false;
            }

            *out_value = constant_convert(value, expr->ty);
            return true;
        }
        case EXPR_CONDITIONAL: {
            int64_t condition;

            if (!eval_constant(expr->conditional.condition, &condition))
                return false;

            return eval_constant(condition
                    ? expr->conditional.then_expr
                    : expr->conditional.else_expr, out_value);
        }
        case EXPR_BINARY: {
            token_kind op = expr->binary.op.kind;
            int64_t a, b;

            if (!eval_constant(expr->binary.left, &a))
                return false;

            if (op == TOKEN_AND_AND && !a) {
                *out_value = 0;
                return true;
            }

            if (op == TOKEN_OR_OR && a) {
                *out_value = 1;
                return true;
            }

            if (!eval_constant(expr->binary.right, &b))
                return false;

            uint64_t ua = (uint64_t)a;
            uint64_t ub = (uint64_t)b;

            switch (op) {
                case TOKEN_PLUS:
                    a = (int64_t)ua + ub;
                    break;
                case TOKEN_MINUS:
                    a = (int64_t)ua - ub;
                    break;
                case TOKEN_STAR:
                    a = (int64_t)ua * ub;
                    break;
                case TOKEN_SLASH:
                    if (b == 0)
                        return false;
                    a = (b == -1) ? (int64_t)(0 - ua) : a / b;
                    break;
                case TOKEN_PERCENT:
                    if (b == 0)
                        return false;
                    a = (b == -1) ? 0 : a % b;
                    break;
                case TOKEN_AND:
                    a = a & b;
                    break;
                case TOKEN_OR:
                    a = a | b;
                    break;
                case TOKEN_CARET:
                    a = a ^ b;
                    break;
                case TOKEN_LESS_LESS:
                case TOKEN_GREATER_GREATER:
                    if (b < 0 || b >= (int64_t)type_size(expr->ty) * 8)
                        return false;
                    a = (op == TOKEN_LESS_LESS)
                        ? (int64_t)(ua << b)
                        : a >> b;
                    break;
                case TOKEN_EQUAL_EQUAL:
                    a = a == b;
                    break;
                case TOKEN_BANG_EQUAL:
                    a = a != b;
                    break;
                case TOKEN_LESS:
                    a = a < b;
                    break;
                case TOKEN_LESS_EQUAL:
                    a = a <= b;
                    break;
                case TOKEN_GREATER:
                    a = a > b;
                    break;
                case TOKEN_GREATER_EQUAL:
                    a = a >= b;
                    break;
                case TOKEN_AND_AND:
                case TOKEN_OR_OR:
                    a = b != 0;
                    break;
                default:
                    return false;
            }

            *out_value = constant_convert(a, expr->ty);
            return true;
        }
        default:
            return false;
    }
}

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
                error(&expr->tok, 
                        "Undeclared identifier '%.*s'",
                        expr->tok.len,
                        expr->tok.start);
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
            expr->is_lvalue = false;

            if (!type_is_arithmetic(expr->unary.operand->ty)) {
                error(&expr->tok, "Invalid operand type");
                expr->ty = type_int();
                break;
            }

            expr->ty = expr->unary.op.kind == TOKEN_BANG
                ? type_int()
                : expr->unary.operand->ty;

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
            analyze_expr(expr->binary.left);
            analyze_expr(expr->binary.right);

            type_t *left_ty = expr->binary.left->ty;
            type_t *right_ty = expr->binary.right->ty;
            expr->is_lvalue = false;

            if (!type_is_arithmetic(left_ty) ||
                    !type_is_arithmetic(right_ty)) {
                error(&expr->tok, "Invalid operand type");
                expr->ty = type_int();
                break;
            }

            type_t *common_ty =
                type_usual_arithmetic_conversion(left_ty, right_ty);

            switch (expr->binary.op.kind) {
                case TOKEN_AND_AND:
                case TOKEN_OR_OR:
                    expr->ty = type_int();
                    break;
                case TOKEN_LESS_LESS:
                case TOKEN_GREATER_GREATER:
                    expr->ty = left_ty;
                    break;
                case TOKEN_EQUAL_EQUAL:
                case TOKEN_BANG_EQUAL:
                case TOKEN_LESS:
                case TOKEN_LESS_EQUAL:
                case TOKEN_GREATER:
                case TOKEN_GREATER_EQUAL:
                    convert_to_type(&expr->binary.left, common_ty);
                    convert_to_type(&expr->binary.right, common_ty);
                    expr->ty = type_int();
                    break;
                default:
                    convert_to_type(&expr->binary.left, common_ty);
                    convert_to_type(&expr->binary.right, common_ty);
                    expr->ty = common_ty;
                    break;
            }
            break;
        case EXPR_ASSIGNMENT:
            analyze_expr(expr->assignment.lvalue);
            analyze_expr(expr->assignment.rvalue);

            type_t *lvalue_ty = expr->assignment.lvalue->ty;
            type_t *rvalue_ty = expr->assignment.rvalue->ty;

            expr->ty = lvalue_ty;
            expr->is_lvalue = false;

            if (!(expr->assignment.lvalue->is_lvalue && type_is_object(expr->ty))) {
                error(&expr->tok,
                        "Left side is not assignable");
                break;
            }

            if (!type_is_arithmetic(rvalue_ty)) {
                error(&expr->tok, "Invalid operand types");
                break;
            }

            token_kind op = expr->assignment.op.kind;
            if (op == TOKEN_EQUAL) {
                // a = ... is converted to a type
                convert_to_type(&expr->assignment.rvalue, lvalue_ty);
            } else if (op == TOKEN_LESS_LESS_EQUAL || op == TOKEN_GREATER_GREATER_EQUAL) {
                // a <<= is computed in type of a
                expr->assignment.op_ty = lvalue_ty;
            } else {
                // a + b is computed in common type but
                // result is in a type
                type_t *common_ty = type_usual_arithmetic_conversion(lvalue_ty, rvalue_ty);
                expr->assignment.op_ty = common_ty;
                convert_to_type(&expr->assignment.rvalue, common_ty);
            }
            break;
        case EXPR_CONDITIONAL:
            analyze_expr(expr->conditional.condition);
            analyze_expr(expr->conditional.then_expr);
            analyze_expr(expr->conditional.else_expr);

            type_t *then_ty = expr->conditional.then_expr->ty;
            type_t *else_ty = expr->conditional.else_expr->ty;
            if (type_is_arithmetic(then_ty) &&
                    type_is_arithmetic(else_ty)) {
                type_t *common =
                    type_usual_arithmetic_conversion(then_ty, else_ty);
                convert_to_type(&expr->conditional.then_expr, common);
                convert_to_type(&expr->conditional.else_expr, common);
                expr->ty = common;
            } else if (type_is_void(then_ty) && type_is_void(else_ty)) {
                expr->ty = type_void();
            } else {
                error(&expr->tok, "Incompatible operand types");
                expr->ty = type_int();
            }
            break;
        case EXPR_CALL:
            analyze_expr(expr->call.callee);
            expr->is_lvalue = false;

            type_t *fn_ty = expr->call.callee->ty;
            if (!type_is_function(fn_ty)) {
                error(&expr->tok, "Called object is not a function");

                LIST_FOREACH(arg, &expr->call.args) {
                    analyze_expr(arg);
                }

                expr->ty = type_int();
                break;
            }

            size_t param_count = fn_ty->function.params.count;
            size_t arg_count = 0;
            ast_expr_t *last = nullptr;

            for (ast_expr_t **slot = &expr->call.args.head;
                    *slot; slot = &(*slot)->next) {
                analyze_expr(*slot);

                if (arg_count < param_count) {
                    if (type_is_arithmetic((*slot)->ty)) {
                        type_t *param_ty = *VECTOR_GET(&fn_ty->function.params, type_t *, arg_count);
                        convert_to_type(slot, param_ty);
                    } else {
                        error(&(*slot)->tok, "Invalid argument type");
                    }
                }

                last = *slot;
                arg_count++;
            }
            expr->call.args.tail = last;

            if (arg_count != param_count)
                error(&expr->tok, "Wrong number of arguments");

            expr->ty = fn_ty->function.return_ty;
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

static void analyze_block(ast_stmt_t *block, bool new_scope);
static void analyze_declaration_list(ast_stmt_t *stmt);
static void validate_for_init(ast_stmt_t *init);

static void
resolve_gotos(void)
{
    for (size_t i = 0; i < sema.fn_ctx.gotos.count; i++) {
        ast_stmt_t *stmt = *VECTOR_GET(&sema.fn_ctx.gotos, ast_stmt_t *, i);
        token_t *label = &stmt->goto_stmt.label;

        ast_stmt_t *target = hashmap_get(
            &sema.fn_ctx.labels,
            label->start,
            label->len);

        if (!target) {
            error(label, "Use of undeclared label '%.*s'",
                    (int)label->len, label->start);
        }

        stmt->goto_stmt.target = target;
    }
}

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
            analyze_declaration_list(stmt);
            break;
        case STMT_BLOCK:
            analyze_block(stmt, true);
            break;
        case STMT_RETURN:
            type_t *return_ty = sema.fn_ctx.return_ty;
            
            if (stmt->return_stmt.expr)
                analyze_expr(stmt->return_stmt.expr);

            if (type_is_void(return_ty)) {
                if (stmt->return_stmt.expr)
                    error(&stmt->tok,
                            "Void functions should not return a value");
            } else if (!stmt->return_stmt.expr) {
                error(&stmt->tok,
                        "Non-void functions must return a value");
            } else if (!type_is_arithmetic(stmt->return_stmt.expr->ty)) {
                error(&stmt->tok,
                        "Invalid return value type");
            }
            else {
                convert_to_type(&stmt->return_stmt.expr, return_ty);
            }
            break;
        case STMT_IF:
            analyze_expr(stmt->if_stmt.condition);
            analyze_stmt(stmt->if_stmt.then_stmt);
            analyze_stmt(stmt->if_stmt.else_stmt);
            break;
        case STMT_WHILE:
        case STMT_DOWHILE:
        case STMT_FOR: {
            sema.scope = scope_push(sema.scope);

            validate_for_init(stmt->loop.init);
            analyze_stmt(stmt->loop.init);
            analyze_expr(stmt->loop.condition);
            analyze_expr(stmt->loop.post);

            ast_stmt_t *saved_breakable = sema.fn_ctx.breakable;
            ast_stmt_t *saved_loop = sema.fn_ctx.loop;
            sema.fn_ctx.breakable = stmt;
            sema.fn_ctx.loop = stmt;

            analyze_stmt(stmt->loop.body);

            sema.fn_ctx.breakable = saved_breakable;
            sema.fn_ctx.loop = saved_loop;
            
            sema.scope = scope_pop(sema.scope);
            break;
        }
        case STMT_SWITCH: {
            analyze_expr(stmt->switch_stmt.condition);

            if (!type_is_integer(stmt->switch_stmt.condition->ty)) {
                error(&stmt->tok, "Switch condition must be integer");
                stmt->switch_stmt.condition->ty = type_int();
            }

            ast_stmt_t *saved_sw = sema.fn_ctx.sw;
            ast_stmt_t *saved_breakable = sema.fn_ctx.breakable;
            ast_stmt_t *saved_last_case = sema.fn_ctx.last_case;
            sema.fn_ctx.breakable = stmt;
            sema.fn_ctx.loop = stmt;
            sema.fn_ctx.last_case = nullptr;

            analyze_stmt(stmt->switch_stmt.body);

            sema.fn_ctx.breakable = saved_breakable;
            sema.fn_ctx.last_case = saved_last_case;
            sema.fn_ctx.sw = saved_sw;
            break;
        }
        case STMT_CASE:
            ast_stmt_t *sw = sema.fn_ctx.sw;

            analyze_expr(stmt->case_stmt.expr);

            if (!sw) {
                error(&stmt->tok, "'case' label used outside of 'switch'");
            } else if (!type_is_integer(stmt->case_stmt.expr->ty)) {
                error(&stmt->tok, "Case value must be integer");
            } else {
                convert_to_type(&stmt->case_stmt.expr,
                        sw->switch_stmt.condition->ty);

                int64_t value;
                if (!eval_constant(stmt->case_stmt.expr, &value)) {
                    error(&stmt->tok, "Case value must be constan");
                } else {
                    bool duplicate = false;
                    for (ast_stmt_t *c = sw->switch_stmt.cases;
                            c; c = c->case_stmt.next_case) {
                        if (c->case_stmt.value == value)
                            duplicate = true;
                    }

                    if (duplicate) {
                        error(&stmt->tok, "Duplicate case value");
                    } else {
                        stmt->case_stmt.value = value;

                        if (sema.fn_ctx.last_case)
                            sema.fn_ctx.last_case->case_stmt.next_case = stmt;
                        else
                            sw->switch_stmt.cases = stmt;

                        sema.fn_ctx.last_case = stmt;
                    }
                }
            }
            
            analyze_stmt(stmt->case_stmt.stmt);
            break;
        case STMT_DEFAULT:
            if (!sema.fn_ctx.sw)
                error(&stmt->tok, "'default' label used outside of 'switch'");
            else if (sema.fn_ctx.sw->switch_stmt.default_case) {
                error(&stmt->tok, "Duplicate 'default' label");
            }
            else 
                sema.fn_ctx.sw->switch_stmt.default_case = stmt;

            analyze_stmt(stmt->default_stmt.stmt);
            break;
        case STMT_BREAK:
            if (!sema.fn_ctx.breakable)
                error(&stmt->tok, "'break' statement outside of loop or switch");
            else
                stmt->break_stmt.target = sema.fn_ctx.breakable;
            break;
        case STMT_CONTINUE:
            if (!sema.fn_ctx.loop)
                error(&stmt->tok, "'continue' statement outside of loop");
            else
                stmt->continue_stmt.target = sema.fn_ctx.loop;
            break;
        case STMT_LABEL:
            token_t *tok = &stmt->label_stmt.label;

            if (hashmap_get(
                        &sema.fn_ctx.labels,
                        tok->start,
                        tok->len)) {
                error(&stmt->tok, "Duplicate label definition");
            } else {
                hashmap_set(
                        &sema.fn_ctx.labels,
                        tok->start,
                        tok->len,
                        stmt);
            }

            analyze_stmt(stmt->label_stmt.stmt);
            break;
        case STMT_GOTO:
            vector_push(&sema.fn_ctx.gotos, &stmt);
            break;
    }
}

static void
analyze_block(ast_stmt_t *block, bool new_scope)
{
    if (new_scope)
        sema.scope = scope_push(sema.scope);

    LIST_FOREACH(stmt, &block->block.items)
        analyze_stmt(stmt);

    if (new_scope)
        sema.scope = scope_pop(sema.scope);
}

/*
 * Declaration analysis
 */

static void
validate_function_params(ast_decl_t *decl)
{
    hashmap names;
    hashmap_init(&names);

    LIST_FOREACH(param, &decl->function.params) {
        if (param->kind != DECL_OBJECT) {
            error(&param->name, "Parameters must be object type");
            continue;
        }

        if (type_is_void(param->ty))
            error(&param->name, "Parameter can't have type 'void'");

        if (param->sc != STORAGE_CLASS_NONE &&
                param->sc != STORAGE_CLASS_REGISTER) {
            error(&param->name, "Only 'register' storage class can be used as parameter");
        }

        if (param->name.len > 0) {
            if (hashmap_get(&names, param->name.start, param->name.len)) {
                    error(&param->name, "Duplicate parameter name '%.*s'", (int)param->name.len, param->name.start);
            }

            hashmap_set(&names, param->name.start, param->name.len, param);
        }
    }

    hashmap_free(&names);
}

static void
validate_declaration(ast_decl_t *decl)
{
    if (is_global_scope() &&
            (decl->sc == STORAGE_CLASS_AUTO ||
             decl->sc == STORAGE_CLASS_REGISTER)) {
        error(&decl->name, "Illegal storage class at file scope");
    }

    /* Validate function */
    if (decl->kind == DECL_FUNCTION) {
        validate_function_params(decl);

        if (!is_global_scope() &&
                decl->sc != STORAGE_CLASS_NONE &&
                decl->sc != STORAGE_CLASS_EXTERN) {
            error(&decl->name,
                    "Block-scope function declaration may only use 'extern'");
        }

        if (decl->function.body &&
                decl->sc != STORAGE_CLASS_NONE &&
                decl->sc != STORAGE_CLASS_EXTERN &&
                decl->sc != STORAGE_CLASS_STATIC) {
            error(&decl->name,
                    "Function definition may only use 'extern' or 'static'");
        }

        return;
    }

    /* Validate object */
    if (type_is_void(decl->ty))
        error(&decl->name, "Objects cannot have type 'void'");

    if (!is_global_scope() &&
            decl->sc == STORAGE_CLASS_EXTERN && decl->object.init) {
        error(&decl->name, "Block-scope extern object cannot have an initializer");
    }
}

static void
validate_for_init(ast_stmt_t *init)
{
    if (!init || init->kind != STMT_DECL)
        return;

    LIST_FOREACH(decl, &init->decl.decls) {
        if (decl->kind != DECL_OBJECT) {
            error(&decl->name, "For-initializer must declare an object");
        }

        if (decl->sc != STORAGE_CLASS_NONE &&
                decl->sc != STORAGE_CLASS_AUTO &&
                decl->sc != STORAGE_CLASS_REGISTER) {
            error(&decl->name, "Illegal storage-cllass for for-initializer");
        }
    }
}

static linkage
inherited_linkage(symbol_t *prior_visible)
{
    if (prior_visible && prior_visible->linkage != LINKAGE_NONE)
        return prior_visible->linkage;

    return LINKAGE_EXTERNAL;
}

static linkage
determine_linkage(ast_decl_t *decl, symbol_t *prior_visible)
{
    // Parameters have no linkage
    if (decl->is_parameter)
        return LINKAGE_NONE;

    // Functions: 'static' is internal, everything else 'extern'
    if (decl->kind == DECL_FUNCTION) {
        if (decl->sc == STORAGE_CLASS_STATIC)
            return LINKAGE_INTERNAL;

        return inherited_linkage(prior_visible);
    }

    // Block-scope
    if (decl->sc == STORAGE_CLASS_EXTERN)
        return inherited_linkage(prior_visible);

    if (is_global_scope()) {
        if (decl->sc == STORAGE_CLASS_STATIC)
            return LINKAGE_INTERNAL;

        if (decl->sc == STORAGE_CLASS_NONE)
            return LINKAGE_EXTERNAL;
    }

    // Block-scope objects at file-scope (already reported errors)
    return LINKAGE_NONE;
}

static storage_duration
determine_storage_duration(ast_decl_t *decl)
{
    if (decl->kind == DECL_FUNCTION)
        return STORAGE_DURATION_STATIC;

    if (is_global_scope())
        return STORAGE_DURATION_STATIC;

    if (decl->sc == STORAGE_CLASS_STATIC ||
            decl->sc == STORAGE_CLASS_EXTERN)
        return STORAGE_DURATION_STATIC;

    return STORAGE_DURATION_AUTO;
}

static void
classify_definition(ast_decl_t *decl)
{
    decl->is_definition = false;
    decl->is_tentative = false;

    // int f() {} vs int f();
    if (decl->kind == DECL_FUNCTION) {
        decl->is_definition = decl->function.body != nullptr;
        return;
    }

    // Initializer always makes a definition
    if (decl->object.init) {
        decl->is_definition = true;
        return;
    }

    if (decl->sc == STORAGE_CLASS_EXTERN)
        return;

    if (decl->sd == STORAGE_DURATION_STATIC) {
        decl->is_tentative = true;
        return;
    }

    // Automatic objects and parameters
    decl->is_definition = true;
}

static void
merge_redeclaration(
        ast_decl_t *decl,
        symbol_t *sym,
        bool install)
{
    const char *name = decl->name.start;
    int len = (int)decl->name.len;

    if (decl->link != sym->linkage)
        error(&decl->name, "Conflicting linkage for '%.*s'", len, name);

    // Also catches a function redeclared as an object and the reverse
    if (!type_compatible(decl->ty, sym->ty))
        error(&decl->name, "Conflicting types for '%.*s'", len, name);

    if (decl->kind == DECL_FUNCTION && decl->is_definition) {
        if (sym->defined)
            error(&decl->name, "Redefinition of '%.*s'", len, name);

        // The backend wants the declaration that has the body
        sym->defined = true;
        sym->decl = decl;
    }

    if (decl->is_tentative && sym->init == INIT_NONE)
        sym->init = INIT_TENTATIVE;

    decl->sym = sym;

    if (install) {
        hashmap_set(
            &sema.scope->ordinary,
            decl->name.start,
            decl->name.len,
            sym);
    }
}

static void
bind_declaration_symbol(ast_decl_t *decl)
{
    const char *name = decl->name.start;
    const size_t len = decl->name.len;
    symbol_t *prior_visible = scope_lookup(sema.scope, name, len);

    decl->link = determine_linkage(decl, prior_visible);
    decl->sd = determine_storage_duration(decl);
    classify_definition(decl);

    // Same scope: only declarations with linkage may be repeated
    symbol_t *prior_current = scope_lookup_current(sema.scope, name, len);
    if (prior_current) {
        if (decl->link == LINKAGE_NONE ||
                prior_current->linkage == LINKAGE_NONE) {
            error(&decl->name,
                    "Redeclaration of '%.*s'", len, name);
            decl->sym = prior_current;
            return;
        }

        merge_redeclaration(decl, prior_current, false);
        return;
    }

    if (decl->link != LINKAGE_NONE) {
        symbol_t *prior_linked = hashmap_get(&sema.linked_symbols, name, len);

        if (prior_linked) {
            merge_redeclaration(decl, prior_linked, true);
            return;
        }
    }

    symbol_t *sym = symbol_new(decl);
    hashmap_set(&sema.scope->ordinary, name, len, sym);

    if (decl->link != LINKAGE_NONE)
        hashmap_set(&sema.linked_symbols, name, len, sym);

    decl->sym = sym;
}

static void
analyze_object_initializer(ast_decl_t *decl)
{
    analyze_expr(decl->object.init);

    if (!type_is_arithmetic(decl->object.init->ty)) {
        error(&decl->name, "Invalid initializer type");
        return;
    }

    // Skip void
    if (!type_is_arithmetic(decl->ty))
        return;

    convert_to_type(&decl->object.init, decl->ty);

    if (decl->sd != STORAGE_DURATION_STATIC)
        return;

    int64_t value;
    if (!eval_constant(decl->object.init, &value)) {
        error(&decl->object.init->tok,
                "Initializer with static storage must be a constant");
        return;
    }

    if (decl->sym->init == INIT_CONSTANT) {
        error(&decl->name, "Redeclaration of '%.*s'", decl->name.len, decl->name.start);
        return;
    }

    decl->sym->init = INIT_CONSTANT;
    decl->sym->init_value = value;
}

static void
analyze_function(ast_decl_t *fn)
{
    sema.fn_ctx = (function_ctx_t){
        .decl = fn,
        .return_ty = fn->ty->function.return_ty
    };
    hashmap_init(&sema.fn_ctx.labels);
    VECTOR_INIT(&sema.fn_ctx.gotos, ast_stmt_t *);

    // Parameters share scope with function body
    sema.scope = scope_push(sema.scope);

    LIST_FOREACH(param, &fn->function.params) {
        if (param->name.len == 0) {
            error(&param->name, "Parameter name omitted in function declaration");
            continue;
        }

        bind_declaration_symbol(param);
    }

    analyze_block(fn->function.body, false);
    resolve_gotos();

    sema.scope = scope_pop(sema.scope);

    vector_free(&sema.fn_ctx.gotos);
    hashmap_free(&sema.fn_ctx.labels);
}

static void
analyze_declaration(ast_decl_t *decl)
{
    validate_declaration(decl);
    bind_declaration_symbol(decl);

    if (decl->kind == DECL_FUNCTION && decl->function.body)
        analyze_function(decl);

    if (decl->kind == DECL_OBJECT && decl->object.init)
        analyze_object_initializer(decl);
}

static void
analyze_declaration_list(ast_stmt_t *stmt)
{
    LIST_FOREACH(decl, &stmt->decl.decls)
        analyze_declaration(decl);
}

static void
finish_tentative_declarations(sema_result_t *result)
{
    LIST_FOREACH(sym, &sema.result->symbols) {
        if (sym->init == INIT_TENTATIVE) {
            sym->init = INIT_CONSTANT;
            sym->init_value = 0;
        }
        result->symbol_count++;
    }
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
    sema.scope = scope_push(nullptr); // global/file scope

    LIST_FOREACH(decl, &program->decls)
        analyze_declaration(decl);

    finish_tentative_declarations(result);

    sema.scope = scope_pop(sema.scope);
    hashmap_free(&sema.linked_symbols);

    return !diagnostics_had_error();
}
