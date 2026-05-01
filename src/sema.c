#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "sema.h"
#include "ast.h"
#include "lexer.h"
#include "type.h"
#include "base/hash_map.h"

struct scope {
    struct scope *parent;
    hash_map ordinary;
};

struct loop_switch_ctx {
    const char *break_label;
    const char *continue_label;
};

static struct scope *global_scope;
static struct scope *current_scope;
static struct decl *current_function;

static hash_map labels;

static int unique_counter = 0;
static bool had_error = false;

static void error(struct token *tok, const char *message)
{
    int col = (int)(tok->start - tok->line_start);

    fprintf(stderr, "Error at line %d, col %d: %s\n", tok->line, col, message);

    const char *line_end = tok->line_start;
    while (*line_end != '\0' && *line_end != '\n')
        line_end++;
    fprintf(stderr, "  %.*s\n", (int)(line_end - tok->line_start), tok->line_start);

    fprintf(stderr, "  %*s", col, "");
    for (int i = 0; i < (tok->length > 0 ? tok->length : 1); i++)
        fputc('^', stderr);
    fputc('\n', stderr);

    had_error = true;
}

static char *make_unique(const char *name, int length)
{
    int n = snprintf(NULL, 0, "%.*s.%d", length, name, unique_counter);
    char *buf = malloc(n + 1);
    snprintf(buf, n + 1, "%.*s.%d", length, name, unique_counter++);

    return buf;
}

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

static bool is_file_scope()
{
    return current_scope == global_scope;
}

static struct symbol *scope_lookup_current(struct scope *s, const char *name, int length)
{
    if (!s)
        return NULL;

    return hashmap_get(&s->ordinary, name, length);
}

static struct symbol *scope_lookup_visible(struct scope *s, const char *name, int length)
{
    for (struct scope *scp = s; scp != NULL; scp = scp->parent) {
        struct symbol *sym = hashmap_get(&scp->ordinary, name, length);
        if (sym)
            return sym;
    }

    return NULL;
}

static struct symbol *symbol_new(struct decl *d)
{
    struct symbol *sym = calloc(1, sizeof(struct symbol));
    sym->kind = d->kind == DECL_FUNCTION ? SYM_FUNCTION : SYM_OBJECT;
    sym->name = d->name.start;
    sym->name_len = d->name.length;
    sym->ty = d->ty;
    sym->decl = d;
    sym->linkage = d->linkage;
    sym->storage_duration = d->storage_duration;
    sym->defined = d->is_definition;
    sym->tentative = d->is_tentative;

    if (d->linkage == LINK_EXTERNAL)
        sym->ir_name = token_to_cstr(d->name);
    else
        sym->ir_name = make_unique(d->name.start, d->name.length);

    return sym;
}

static enum linkage compute_linkage(struct decl *d, struct symbol *prior_visible)
{
    if (d->is_parameter)
        return LINK_NONE;

    if (d->kind == DECL_FUNCTION) {
        // No storage-class -> external linkage
        if (d->storage_class == SC_STATIC)
            return LINK_INTERNAL;

        /*
         * Function declarations with no storage-class behave as extern.
         * Therefore they inherit visible internal/external linkage.
         */
        if (prior_visible && (prior_visible->linkage == LINK_INTERNAL ||
            prior_visible->linkage == LINK_EXTERNAL)) {
            return prior_visible->linkage;
        }

        return LINK_EXTERNAL;
    }

    if (is_file_scope()) {
        if (d->storage_class == SC_STATIC)
            return LINK_INTERNAL;

        /*
         * File scope extern inherits visible prior
         * internal/external linkage, otherwise external
         */
        if (d->storage_class == SC_EXTERN) {
            if (prior_visible &&
                (prior_visible->linkage == LINK_INTERNAL ||
                prior_visible->linkage == LINK_EXTERNAL)) {
                return prior_visible->linkage;
            }

            return LINK_EXTERNAL;
        }

        return LINK_EXTERNAL;
    }

    /*
     * Block-scope extern object declaration may refer to an existing linked
     * declaration. Otherwise it declares an external object.
     */
    if (d->storage_class == SC_EXTERN) {
        if (prior_visible &&
            (prior_visible->linkage == LINK_INTERNAL ||
             prior_visible->linkage == LINK_EXTERNAL)) {
            return prior_visible->linkage;
        }

        return LINK_EXTERNAL;
    }

    return LINK_NONE;
}

static enum storage_duration compute_storage_duration(struct decl *d)
{
    if (d->kind == DECL_FUNCTION)
        return SD_STATIC;

    if (d->is_parameter)
        return SD_AUTO;

    if (d->storage_class == SC_STATIC)
        return SD_STATIC;

    if (d->linkage == LINK_INTERNAL || d->linkage == LINK_EXTERNAL)
        return SD_STATIC;

    return SD_AUTO;
}

static void classify_definition(struct decl *d)
{
    d->is_definition = false;
    d->is_tentative = false;

    if (d->kind == DECL_FUNCTION) {
        if (d->func.body)
            d->is_definition = true;

        return;
    }

    if (!is_file_scope()) {
        if (d->storage_class != SC_EXTERN)
            d->is_definition = true;

        return;
    }

    /*
     * C11 6.9.2:
     *
     * File-scope object with initializer is an external definition.
     * File-scope object without initializer and no storage class or static
     * is a tentative definition.
     */
    if (d->var.init) {
        d->is_definition = true;
    } else if (d->storage_class == SC_NONE ||
               d->storage_class == SC_STATIC) {
        d->is_tentative = true;
    }
}

static void validate_function_params(struct decl *fn)
{
    for (struct decl *p = fn->func.params; p; p = p->next) {
        if (type_is_void(p->ty))
            error(&p->name, "Function parameter cannot be type void");

        if (p->storage_class != SC_NONE && p->storage_class != SC_REGISTER)
            error(&p->name, "Only 'register' storage class can be used as a parameter");
    }
}

static void validate_decl(struct decl *d)
{
    if (is_file_scope() &&
        (d->storage_class == SC_AUTO || d->storage_class == SC_REGISTER)) {
        error(&d->name, "Illegal storage at file-scope");
    }

    // TODO: Check typedef stuff

    if (d->kind == DECL_FUNCTION) {
        validate_function_params(d);

        if (!is_file_scope() && d->storage_class != SC_NONE &&
            d->storage_class != SC_EXTERN) {
            error(&d->name, "Block-scope function declarations may only use extern");
        }

        if (d->func.body && d->storage_class != SC_NONE &&
            d->storage_class != SC_EXTERN && d->storage_class != SC_STATIC) {
            error(&d->name, "Function definition may only use extern or static");
        }

        return;
    }

    if (type_is_void(d->ty))
        error(&d->name, "Object cannot have type void");

    if (!is_file_scope() && d->storage_class == SC_EXTERN && d->var.init)
        error(&d->name, "Block-scope extern cannot have an initializer");
}

static struct symbol *declare_symbol(struct decl *d)
{
    struct symbol *prior_visible = scope_lookup_visible(current_scope,
            d->name.start, d->name.length);

    d->linkage = compute_linkage(d, prior_visible);
    d->storage_duration = compute_storage_duration(d);

    classify_definition(d);

    struct symbol *prior_current = scope_lookup_current(current_scope,
            d->name.start, d->name.length);

    if (prior_current) {
        if (d->linkage == LINK_NONE || prior_current->linkage == LINK_NONE) {
            error(&d->name, "Duplicate declaration");
            d->sym = prior_current;
            d->ir_name = prior_current->ir_name;
            return prior_current;
        }

        if (d->linkage != prior_current->linkage) {
            error(&d->name, "Conflicting linkage for declaration");
            d->sym = prior_current;
            d->ir_name = prior_current->ir_name;
            return prior_current;
        }

        if (!types_compatible(d->ty, prior_current->ty)) {
            error(&d->name, "Conflicting declaration types");
            d->sym = prior_current;
            d->ir_name = prior_current->ir_name;
            return prior_current;
        }

        if (prior_current->defined && d->is_definition) {
            error(&d->name, "Redefinition");
            d->sym = prior_current;
            d->ir_name = prior_current->ir_name;
            return prior_current;
        }

        prior_current->defined |= d->is_definition;
        prior_current->tentative |= d->is_tentative;

        d->sym = prior_current;
        d->ir_name = prior_current->ir_name;

        return prior_current;
    }

    /*
     * Block-scope extern can introduce a local declaration that refers to
     * an already-visible linked symbol.
     */
    if (d->storage_class == SC_EXTERN &&
        prior_visible &&
        prior_visible->linkage != LINK_NONE) {
        if (!types_compatible(prior_visible->ty, d->ty))
            error(&d->name, "Conflicting extern declaration type");

        hashmap_set(&current_scope->ordinary,
                    d->name.start,
                    d->name.length,
                    prior_visible);

        d->sym = prior_visible;
        d->ir_name = prior_visible->ir_name;

        return prior_visible;
    }

    struct symbol *sym = symbol_new(d);

    hashmap_set(&current_scope->ordinary, d->name.start,
                d->name.length, sym);

    d->sym = sym;
    d->ir_name = sym->ir_name;
    
    return sym;
}

static void check_call_args(struct expr *expr)
{
    struct type *fn_ty = expr->call.callee->ty;

    if (!fn_ty->func.has_prototype)
        return;

    int arg_count = 0;
    for (struct expr *arg = expr->call.args; arg; arg = arg->next)
        arg_count++;

    // TODO: Change the error function to handle stuff like "%s %s"
    if (arg_count != fn_ty->func.param_count) {
        error(&expr->tok, "Wrong number of function arguments");
        return;
    }

    struct expr *arg = expr->call.args;
    struct decl *param = fn_ty->func.params;
    for (; arg && param; arg = arg->next, param = param->next) {
        if (!types_compatible(arg->ty, param->ty))
            error(&arg->tok, "Argument type does not match parameter type");
    }
}

static void analyze_expr(struct expr *expr)
{
    if (!expr)
        return;

    switch (expr->kind) {
        case EXPR_INT_LITERAL:
            expr->ty = type_int();
            expr->is_lvalue = false;
            break;

        case EXPR_IDENTIFIER: {
            struct symbol *sym = scope_lookup_visible(current_scope,
                    expr->identifier.name.start, expr->identifier.name.length);

            if (!sym) {
                error(&expr->tok, "Undeclared identifier");
                expr->ty = type_int();
                expr->is_lvalue = false;
                return;
            }

            expr->identifier.sym = sym;
            expr->ty = sym->ty;
            expr->is_lvalue = sym->kind == SYM_OBJECT;
            break;
        }

        case EXPR_ASSIGNMENT: {
            analyze_expr(expr->assignment.lvalue);
            analyze_expr(expr->assignment.rvalue);

            if (!expr->assignment.lvalue->is_lvalue)
                error(&expr->assignment.lvalue->tok, "Left side is not assignable");

            if (!types_compatible(expr->assignment.lvalue->ty,
                        expr->assignment.rvalue->ty))
                error(&expr->tok, "Assignment types are not compatible");

            expr->ty = expr->assignment.lvalue->ty;
            expr->is_lvalue = false;
            break;
        }

        case EXPR_PRE:
        case EXPR_POST:
            analyze_expr(expr->unary.operand);

            if (!expr->unary.operand->is_lvalue)
                error(&expr->tok, "Operand of increment/decrement must be an lvalue");

            expr->ty = expr->unary.operand->ty;
            expr->is_lvalue = false;
            break;

        case EXPR_UNARY:
            analyze_expr(expr->unary.operand);
            expr->ty = expr->unary.operand->ty;
            expr->is_lvalue = false;
            break;

        case EXPR_BINARY:
            analyze_expr(expr->binary.left);
            analyze_expr(expr->binary.right);

            if (!type_is_int(expr->binary.left->ty) || 
                !type_is_int(expr->binary.right->ty))
                error(&expr->tok, "For now we only support int binary ops");
            
            expr->ty = expr->binary.left->ty;
            expr->is_lvalue = false;
            break;

        case EXPR_CONDITIONAL:
            analyze_expr(expr->conditional.condition);
            analyze_expr(expr->conditional.then_expr);
            analyze_expr(expr->conditional.else_expr);

            if (!types_compatible(expr->conditional.then_expr->ty,
                                  expr->conditional.else_expr->ty)) {
                error(&expr->tok,
                      "Conditional expression arms have incompatible types");
            }

            expr->ty = expr->conditional.then_expr->ty;
            expr->is_lvalue = false;
            break;

        case EXPR_CALL:
            analyze_expr(expr->call.callee);

            for (struct expr *arg = expr->call.args; arg; arg = arg->next)
                analyze_expr(arg);

            if (!type_is_function(expr->call.callee->ty)) {
                error(&expr->call.callee->tok, "Called object is not a function");
                expr->ty = type_int();
                expr->is_lvalue = false;
                return;
            }

            check_call_args(expr);

            expr->ty = expr->call.callee->ty->func.return_type;
            expr->is_lvalue = false;
            break;
    }
}

static void analyze_stmt(struct stmt *stmt);

static void analyze_decl_list(struct decl *decls)
{
    if (!decls)
        return;

    for (struct decl *d = decls; d; d = d->next) {
        validate_decl(d);
        declare_symbol(d);

        if (d->kind == DECL_VAR && d->var.init)
            analyze_expr(d->var.init);
    }
}

static void analyze_block(struct block_item *first, bool push_new_scope)
{
    if (!first)
        return;

    struct scope *old_scope = current_scope;
    if (push_new_scope)
        current_scope = scope_push(current_scope);

    for (struct block_item *item = first; item; item = item->next) {
        if (item->kind == BLOCK_ITEM_DECL) {
            analyze_decl_list(item->decls);
        } else {
            analyze_stmt(item->stmt);
        }
    }

    if (push_new_scope) {
        scope_pop(current_scope);
        current_scope = old_scope;
    }

}

static void analyze_stmt(struct stmt *stmt)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_NULL:
        case STMT_BREAK:
        case STMT_CONTINUE:
        case STMT_GOTO:
            break;

        case STMT_EXPR:
            analyze_expr(stmt->expr_stmt.expr);
            break;

        case STMT_RETURN: {
            struct type *ret_ty = current_function->ty->func.return_type;

            if (stmt->return_stmt.expr)
                analyze_expr(stmt->return_stmt.expr);

            if (type_is_void(ret_ty)) {
                if (stmt->return_stmt.expr)
                    error(&stmt->tok, "'void' function should not return a value");
            } else {
                if (!stmt->return_stmt.expr)
                    error(&stmt->tok, "Non-void function should return a value");
                else if (!types_compatible(ret_ty, stmt->return_stmt.expr->ty))
                    error(&stmt->tok, "Return type mismatch");
            }
            break;
        }

        case STMT_IF:
            analyze_expr(stmt->if_stmt.condition);
            analyze_stmt(stmt->if_stmt.then_stmt);
            analyze_stmt(stmt->if_stmt.else_stmt);
            break;

        case STMT_FOR: {
            struct scope *old_scope = current_scope;
            current_scope = scope_push(current_scope);

            if (stmt->for_stmt.init) {
                if (stmt->for_stmt.init->is_decl)
                    analyze_decl_list(stmt->for_stmt.init->decls);
                else
                    analyze_expr(stmt->for_stmt.init->expr);
            }

            analyze_expr(stmt->for_stmt.condition);
            analyze_expr(stmt->for_stmt.post);
            analyze_stmt(stmt->for_stmt.body);

            scope_pop(current_scope);
            current_scope = old_scope;
            break;
        }

        case STMT_WHILE:
            analyze_expr(stmt->while_stmt.condition);
            analyze_stmt(stmt->while_stmt.body);
            break;

        case STMT_DOWHILE:
            analyze_stmt(stmt->dowhile_stmt.body);
            analyze_expr(stmt->dowhile_stmt.condition);
            break;

        case STMT_SWITCH:
            analyze_expr(stmt->switch_stmt.condition);
            analyze_stmt(stmt->switch_stmt.body);
            break;

        case STMT_CASE:
            analyze_expr(stmt->case_stmt.value);
            analyze_block(stmt->case_stmt.items, false);
            break;

        case STMT_DEFAULT:
            analyze_block(stmt->default_stmt.items, false);
            break;

        case STMT_BLOCK:
            analyze_block(stmt->block.items, true);
            break;

        case STMT_LABEL:
            analyze_stmt(stmt->label_stmt.stmt);
            break;
    }
}

static void collect_labels_stmt(struct stmt *stmt);
static void collect_labels_items(struct block_item *item)
{
    for (struct block_item *i = item; i; i = i->next)
        if (i->kind == BLOCK_ITEM_STMT)
            collect_labels_stmt(i->stmt);
}

static void collect_labels_stmt(struct stmt *stmt)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_LABEL: {
            struct token *tok = &stmt->label_stmt.name;

            if (hashmap_get(&labels, tok->start, tok->length))
                error(tok, "Duplicate label definition");
            else
                hashmap_set(&labels, tok->start, tok->length, stmt);

            collect_labels_stmt(stmt->label_stmt.stmt);
            break;
        }

        case STMT_IF:
            collect_labels_stmt(stmt->if_stmt.then_stmt);
            collect_labels_stmt(stmt->if_stmt.else_stmt);
            break;

        case STMT_FOR:
            collect_labels_stmt(stmt->for_stmt.body);
            break;

        case STMT_WHILE:
            collect_labels_stmt(stmt->while_stmt.body);
            break;

        case STMT_DOWHILE:
            collect_labels_stmt(stmt->dowhile_stmt.body);
            break;

        case STMT_SWITCH:
            collect_labels_stmt(stmt->switch_stmt.body);
            break;

        case STMT_CASE:
            collect_labels_items(stmt->case_stmt.items);
            break;

        case STMT_DEFAULT:
            collect_labels_items(stmt->default_stmt.items);
            break;

        case STMT_BLOCK:
            collect_labels_items(stmt->block.items);
            break;

        default:
            break;
    }
}

static void check_gotos_stmt(struct stmt *stmt);
static void check_gotos_items(struct block_item *item)
{
    for (struct block_item *i = item; i; i = i->next)
        if (i->kind == BLOCK_ITEM_STMT)
            check_gotos_stmt(i->stmt);
}

static void check_gotos_stmt(struct stmt *stmt)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_GOTO: {
            struct token *tok = &stmt->goto_stmt.label;

            if (!hashmap_get(&labels, tok->start, tok->length))
                error(tok, "Use of undeclared label");

            break;
        }

        case STMT_LABEL:
            check_gotos_stmt(stmt->label_stmt.stmt);
            break;

        case STMT_IF:
            check_gotos_stmt(stmt->if_stmt.then_stmt);
            check_gotos_stmt(stmt->if_stmt.else_stmt);
            break;

        case STMT_FOR:
            check_gotos_stmt(stmt->for_stmt.body);
            break;

        case STMT_WHILE:
            check_gotos_stmt(stmt->while_stmt.body);
            break;

        case STMT_DOWHILE:
            check_gotos_stmt(stmt->dowhile_stmt.body);
            break;

        case STMT_SWITCH:
            check_gotos_stmt(stmt->switch_stmt.body);
            break;

        case STMT_CASE:
            check_gotos_items(stmt->case_stmt.items);
            break;

        case STMT_DEFAULT:
            check_gotos_items(stmt->default_stmt.items);
            break;

        case STMT_BLOCK:
            check_gotos_items(stmt->block.items);
            break;

        default:
            break;
    }
}

static void resolve_break_continue_stmt(struct stmt *stmt, struct loop_switch_ctx *ctx);
static void resolve_break_continue_items(struct block_item *item, struct loop_switch_ctx *ctx)
{
    for (struct block_item *i = item; i; i = i->next)
        if (i->kind == BLOCK_ITEM_STMT)
            resolve_break_continue_stmt(i->stmt, ctx);
}

static void resolve_break_continue_stmt(struct stmt *stmt, struct loop_switch_ctx *ctx)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_FOR: {
            char *b_label = make_unique("b.for", 5);     
            char *c_label = make_unique("c.for", 5);     
            stmt->for_stmt.break_label = b_label;
            stmt->for_stmt.continue_label = c_label;

            struct loop_switch_ctx new_ctx = {
                .break_label = b_label,
                .continue_label = c_label
            };
            resolve_break_continue_stmt(stmt->for_stmt.body, &new_ctx);
            break;
        }

        case STMT_WHILE: {
            char *b_label = make_unique("b.while", 7);     
            char *c_label = make_unique("c.while", 7);     
            stmt->while_stmt.break_label = b_label;
            stmt->while_stmt.continue_label = c_label;

            struct loop_switch_ctx new_ctx = {
                .break_label = b_label,
                .continue_label = c_label
            };
            resolve_break_continue_stmt(stmt->while_stmt.body, &new_ctx);
            break;
        }

        case STMT_DOWHILE: {
            char *b_label = make_unique("b.dowhile", 9);     
            char *c_label = make_unique("c.dowhile", 9);     
            stmt->dowhile_stmt.break_label = b_label;
            stmt->dowhile_stmt.continue_label = c_label;

            struct loop_switch_ctx new_ctx = {
                .break_label = b_label,
                .continue_label = c_label
            };
            resolve_break_continue_stmt(stmt->dowhile_stmt.body, &new_ctx);
            break;
        }

        case STMT_SWITCH: {
            char *b_label = make_unique("b.switch", 7);     
            stmt->switch_stmt.break_label = b_label;

            struct loop_switch_ctx new_ctx = {
                .break_label = b_label,
                .continue_label = ctx ? ctx->continue_label : NULL
            };
            resolve_break_continue_stmt(stmt->switch_stmt.body, &new_ctx);
            break;
        }

        case STMT_BREAK:
            if (!ctx || !ctx->break_label)
                error(&stmt->tok, "'break' statement outside of loop or switch");
            else
                stmt->break_stmt.target_label = ctx->break_label;
            break;
        
        case STMT_CONTINUE:
            if (!ctx || !ctx->continue_label)
                error(&stmt->tok, "'continue' statement outside of loop");
            else
                stmt->continue_stmt.target_label = ctx->continue_label;
            break;

        case STMT_IF:
            resolve_break_continue_stmt(stmt->if_stmt.then_stmt, ctx);
            resolve_break_continue_stmt(stmt->if_stmt.else_stmt, ctx);
            break;

        case STMT_LABEL:
            resolve_break_continue_stmt(stmt->label_stmt.stmt, ctx);
            break;

        case STMT_CASE:
            resolve_break_continue_items(stmt->case_stmt.items, ctx);
            break;

        case STMT_DEFAULT:
            resolve_break_continue_items(stmt->default_stmt.items, ctx);
            break;

        case STMT_BLOCK:
            resolve_break_continue_items(stmt->block.items, ctx);
            break;

        default:
            break;
    }
}

static void check_case_placement_stmt(struct stmt *stmt, int switch_depth);
static void check_case_placement_items(struct block_item *item, int switch_depth)
{
    for (struct block_item *i = item; i; i = i->next)
        if (i->kind == BLOCK_ITEM_STMT)
            check_case_placement_stmt(i->stmt, switch_depth);
}

static void check_case_placement_stmt(struct stmt *stmt, int switch_depth)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_CASE:
            if (switch_depth == 0)
                error(&stmt->tok, "'case' label outside of switch");

            check_case_placement_items(stmt->case_stmt.items, switch_depth);
            break;

        case STMT_DEFAULT:
            if (switch_depth == 0)
                error(&stmt->tok, "'default' label outside of switch");

            check_case_placement_items(stmt->default_stmt.items,
                                       switch_depth);
            break;

        case STMT_SWITCH:
            check_case_placement_stmt(stmt->switch_stmt.body,
                                      switch_depth + 1);
            break;

        case STMT_IF:
            check_case_placement_stmt(stmt->if_stmt.then_stmt,
                                      switch_depth);
            check_case_placement_stmt(stmt->if_stmt.else_stmt,
                                      switch_depth);
            break;

        case STMT_BLOCK:
            check_case_placement_items(stmt->block.items, switch_depth);
            break;

        case STMT_FOR:
            check_case_placement_stmt(stmt->for_stmt.body, switch_depth);
            break;

        case STMT_WHILE:
            check_case_placement_stmt(stmt->while_stmt.body, switch_depth);
            break;

        case STMT_DOWHILE:
            check_case_placement_stmt(stmt->dowhile_stmt.body,
                                      switch_depth);
            break;

        case STMT_LABEL:
            check_case_placement_stmt(stmt->label_stmt.stmt, switch_depth);
            break;

        default:
            break;
    }
}

static void resolve_cases_stmt(struct stmt *stmt, struct switch_annotation *ann);
static void resolve_cases_items(struct block_item *item, struct switch_annotation *ann)
{
    for (struct block_item *i = item; i; i = i->next)
        if (i->kind == BLOCK_ITEM_STMT)
            resolve_cases_stmt(i->stmt, ann);
}

static void append_case_entry(struct switch_annotation *ann, struct stmt *node)
{
    struct case_entry *entry = calloc(1, sizeof(struct case_entry));
    entry->node = node;

    struct case_entry **tail = &ann->cases;

    while (*tail)
        tail = &(*tail)->next;

    *tail = entry;
}

static void resolve_cases_stmt(struct stmt *stmt, struct switch_annotation *ann)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_CASE: {
            /*
             * TODO: This should calculate the constant from case value expr
             */
            if (stmt->case_stmt.value->kind != EXPR_INT_LITERAL) {
                error(&stmt->tok, "'case' must be an integer constant");
                return;
            }

            long value = stmt->case_stmt.value->int_value;

            for (struct case_entry *e = ann->cases; e; e = e->next) {
                if (e->node->kind != STMT_CASE)
                    continue;

                if (e->node->case_stmt.value->int_value == value) {
                    error(&stmt->tok, "Duplicate case value in switch");
                    return;
                }
            }

            stmt->case_stmt.label = make_unique("case", 4);
            append_case_entry(ann, stmt);

            resolve_cases_items(stmt->case_stmt.items, ann);
            break;
        }

        case STMT_DEFAULT: {
            if (ann->default_node) {
                error(&stmt->tok, "Duplicate default labels in switch");
                return;
            }

            stmt->default_stmt.label = make_unique("default", 7);
            ann->default_node = stmt;
            append_case_entry(ann, stmt);

            resolve_cases_items(stmt->default_stmt.items, ann);
            break;
        }

        case STMT_SWITCH:
            /*
             * Nested switch owns its own cases.
             */
            break;

        case STMT_IF:
            resolve_cases_stmt(stmt->if_stmt.then_stmt, ann);
            resolve_cases_stmt(stmt->if_stmt.else_stmt, ann);
            break;

        case STMT_FOR:
            resolve_cases_stmt(stmt->for_stmt.body, ann);
            break;

        case STMT_WHILE:
            resolve_cases_stmt(stmt->while_stmt.body, ann);
            break;

        case STMT_DOWHILE:
            resolve_cases_stmt(stmt->dowhile_stmt.body, ann);
            break;

        case STMT_LABEL:
            resolve_cases_stmt(stmt->label_stmt.stmt, ann);
            break;

        case STMT_BLOCK:
            resolve_cases_items(stmt->block.items, ann);
            break;

        default:
            break;
    }
}

static void resolve_switches_stmt(struct stmt *stmt);
static void resolve_switches_items(struct block_item *item)
{
    for (struct block_item *i = item; i; i = i->next)
        if (i->kind == BLOCK_ITEM_STMT)
            resolve_switches_stmt(i->stmt);
}

static void resolve_switches_stmt(struct stmt *stmt)
{
    if (!stmt)
        return;
    
    switch (stmt->kind) {
        case STMT_SWITCH: {
            struct switch_annotation *ann = calloc(1, sizeof(*ann));

            resolve_cases_stmt(stmt->switch_stmt.body, ann);

            stmt->switch_stmt.annotation = ann;

            // Nested switches
            resolve_switches_stmt(stmt->switch_stmt.body);
            break;
        }
        case STMT_IF:
            resolve_switches_stmt(stmt->if_stmt.then_stmt);
            resolve_switches_stmt(stmt->if_stmt.else_stmt);
            break;

        case STMT_BLOCK:
            resolve_switches_items(stmt->block.items);
            break;

        case STMT_FOR:
            resolve_switches_stmt(stmt->for_stmt.body);
            break;

        case STMT_WHILE:
            resolve_switches_stmt(stmt->while_stmt.body);
            break;

        case STMT_DOWHILE:
            resolve_switches_stmt(stmt->dowhile_stmt.body);
            break;

        case STMT_CASE:
            resolve_switches_items(stmt->case_stmt.items);
            break;

        case STMT_DEFAULT:
            resolve_switches_items(stmt->default_stmt.items);
            break;

        case STMT_LABEL:
            resolve_switches_stmt(stmt->label_stmt.stmt);
            break;

        default:
            break;
    }
}

static void analyze_function_body(struct decl *fn)
{
    if (!fn->func.body)
        return;

    hashmap_init(&labels);

    collect_labels_stmt(fn->func.body);

    struct scope *old_scope = current_scope;
    struct decl *old_function = current_function;

    current_scope = scope_push(old_scope);
    current_function = fn;

    for (struct decl *p = fn->func.params; p; p = p->next) {
        if (!p->name.start) {
            error(&fn->name, "Function definition parameter needs a name");
            continue;
        }

        validate_decl(p);
        declare_symbol(p);
    }

    analyze_block(fn->func.body->block.items, false);

    check_gotos_stmt(fn->func.body);
    check_case_placement_stmt(fn->func.body, 0);
    resolve_break_continue_stmt(fn->func.body, NULL);
    resolve_switches_stmt(fn->func.body);

    scope_pop(current_scope);
    current_scope = old_scope;
    current_function = old_function;

    hashmap_free(&labels);
}

struct ast_program *sema_analysis(struct ast_program *program)
{
    had_error = false;
    global_scope = scope_push(NULL);
    current_scope = global_scope;
    current_function = NULL;

    /*
     * This loop enforces C11 order-based visibility
     */
    for (struct decl *d = program->decls; d; d = d->next) {
        validate_decl(d);
        declare_symbol(d);

        if (d->kind == DECL_VAR && d->var.init)
            analyze_expr(d->var.init);

        if (d->kind == DECL_FUNCTION && d->func.body)
            analyze_function_body(d);
    }
    
    return had_error ? NULL : program;
}
