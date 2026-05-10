#include <stdio.h>

#include "ast.h"
#include "type.h"
#include "lexer.h"

static void indent(int depth)
{
    for (int i = 0; i < depth; i++)
        printf("  ");
}

static const char *storage_class_name(enum storage_class sc)
{
    switch (sc) {
        case SC_NONE:     return "none";
        case SC_EXTERN:   return "extern";
        case SC_STATIC:   return "static";
        case SC_AUTO:     return "auto";
        case SC_REGISTER: return "register";
    }
}

static const char *linkage_name(enum linkage linkage)
{
    switch (linkage) {
        case LINK_NONE:     return "none";
        case LINK_INTERNAL: return "internal";
        case LINK_EXTERNAL: return "external";
    }
}

static const char *storage_duration_name(enum storage_duration sd)
{
    switch (sd) {
        case SD_NONE:   return "none";
        case SD_AUTO:   return "auto";
        case SD_STATIC: return "static";
        case SD_THREAD: return "thread";
    }
}

static void print_expr(struct expr *expr, int depth);
static void print_stmt(struct stmt *stmt, int depth);
static void print_decls(struct decl *decls, int depth);
static void print_block_item(struct block_item *item, int depth);
static void print_type_inline(struct type *ty);

static void print_type_inline(struct type *ty)
{
    if (!ty) {
        printf("nil");
        return;
    }

    switch (ty->kind) {
        case TY_VOID:
            printf("void");
            break;

        case TY_INT:
            printf("int");
            break;

        case TY_FUNCTION:
            printf("(fn ");

            if (!ty->func.has_prototype) {
                printf("(params unspecified)");
            } else if (ty->func.param_count == 0) {
                printf("(params void)");
            } else {
                printf("(params");

                for (struct decl *p = ty->func.params; p; p = p->next) {
                    printf(" ");
                    print_type_inline(p->ty);
                }

                printf(")");
            }

            printf(" -> ");
            print_type_inline(ty->func.return_type);
            printf(")");
            break;
    }
}

static void print_expr_ann(struct expr *expr)
{
    if (!expr)
        return;

    if (expr->ty) {
        printf(" :type ");
        print_type_inline(expr->ty);
    }

    if (expr->is_lvalue)
        printf(" :lvalue true");
}

static void print_named_expr(const char *name, struct expr *expr, int depth)
{
    indent(depth);

    if (!expr) {
        printf("(%s nil)\n", name);
        return;
    }

    printf("(%s\n", name);
    print_expr(expr, depth + 1);
    indent(depth);
    printf(")\n");
}

static void print_named_stmt(const char *name, struct stmt *stmt, int depth)
{
    indent(depth);

    if (!stmt) {
        printf("(%s nil)\n", name);
        return;
    }

    printf("(%s\n", name);
    print_stmt(stmt, depth + 1);
    indent(depth);
    printf(")\n");
}

static void print_expr_list(const char *name, struct expr *exprs, int depth)
{
    indent(depth);
    printf("(%s", name);

    if (!exprs) {
        printf(")\n");
        return;
    }

    printf("\n");

    for (struct expr *e = exprs; e; e = e->next)
        print_expr(e, depth + 1);

    indent(depth);
    printf(")\n");
}

static void print_block_items(struct block_item *items, int depth)
{
    indent(depth);
    printf("(items");

    if (!items) {
        printf(")\n");
        return;
    }

    printf("\n");

    for (struct block_item *item = items; item; item = item->next)
        print_block_item(item, depth + 1);

    indent(depth);
    printf(")\n");
}

static void print_loop_labels(const char *break_label,
                              const char *continue_label,
                              int depth)
{
    if (!break_label && !continue_label)
        return;

    indent(depth);
    printf("(labels");

    if (break_label)
        printf(" (break %s)", break_label);

    if (continue_label)
        printf(" (continue %s)", continue_label);

    printf(")\n");
}

static void print_expr(struct expr *expr, int depth)
{
    if (!expr) {
        indent(depth);
        printf("nil\n");
        return;
    }

    switch (expr->kind) {
        case EXPR_INT_LITERAL:
            indent(depth);
            printf("(int %ld", expr->int_value);
            print_expr_ann(expr);
            printf(")\n");
            break;

        case EXPR_IDENTIFIER:
            indent(depth);
            printf("(ident %s", token_to_cstr(expr->identifier.name));
            print_expr_ann(expr);
            printf(")\n");
            break;

        case EXPR_UNARY:
            indent(depth);
            printf("(unary %s", token_to_cstr(expr->unary.op));
            print_expr_ann(expr);
            printf("\n");
            print_expr(expr->unary.operand, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case EXPR_PRE:
            indent(depth);
            printf("(pre %s", token_to_cstr(expr->unary.op));
            print_expr_ann(expr);
            printf("\n");
            print_expr(expr->unary.operand, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case EXPR_POST:
            indent(depth);
            printf("(post %s", token_to_cstr(expr->unary.op));
            print_expr_ann(expr);
            printf("\n");
            print_expr(expr->unary.operand, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case EXPR_BINARY:
            indent(depth);
            printf("(binary %s", token_to_cstr(expr->binary.op));
            print_expr_ann(expr);
            printf("\n");
            print_expr(expr->binary.left, depth + 1);
            print_expr(expr->binary.right, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case EXPR_ASSIGNMENT:
            indent(depth);
            printf("(assign %s", token_to_cstr(expr->assignment.op));
            print_expr_ann(expr);
            printf("\n");
            print_named_expr("lhs", expr->assignment.lvalue, depth + 1);
            print_named_expr("rhs", expr->assignment.rvalue, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case EXPR_CONDITIONAL:
            indent(depth);
            printf("(conditional");
            print_expr_ann(expr);
            printf("\n");
            print_named_expr("cond", expr->conditional.condition, depth + 1);
            print_named_expr("then", expr->conditional.then_expr, depth + 1);
            print_named_expr("else", expr->conditional.else_expr, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case EXPR_CALL:
            indent(depth);
            printf("(call");
            print_expr_ann(expr);
            printf("\n");
            print_named_expr("callee", expr->call.callee, depth + 1);
            print_expr_list("args", expr->call.args, depth + 1);
            indent(depth);
            printf(")\n");
            break;
    }
}

static void print_decl_attrs(struct decl *d, int depth)
{
    indent(depth);
    printf("(attrs");
    printf(" (storage %s)", storage_class_name(d->storage_class));
    printf(" (linkage %s)", linkage_name(d->linkage));
    printf(" (duration %s)", storage_duration_name(d->storage_duration));

    if (d->is_definition)
        printf(" definition");

    if (d->is_tentative)
        printf(" tentative");

    if (d->is_parameter)
        printf(" parameter");

    if (d->ir_name)
        printf(" (ir-name %s)", d->ir_name);

    printf(")\n");
}

static void print_param_decl(struct decl *p, int depth)
{
    indent(depth);
    printf("(param %s (type ", token_to_cstr(p->name));
    print_type_inline(p->ty);
    printf(")");

    if (p->storage_class != SC_NONE)
        printf(" (storage %s)", storage_class_name(p->storage_class));

    printf(")\n");
}

static void print_param_decls(struct decl *params, int depth)
{
    indent(depth);
    printf("(params");

    if (!params) {
        printf(")\n");
        return;
    }

    printf("\n");

    for (struct decl *p = params; p; p = p->next)
        print_param_decl(p, depth + 1);

    indent(depth);
    printf(")\n");
}

static void print_decl(struct decl *d, int depth)
{
    if (!d) {
        indent(depth);
        printf("nil\n");
        return;
    }

    switch (d->kind) {
        case DECL_VAR:
            indent(depth);
            printf("(var %s\n", token_to_cstr(d->name));

            indent(depth + 1);
            printf("(type ");
            print_type_inline(d->ty);
            printf(")\n");

            print_decl_attrs(d, depth + 1);

            if (d->var.init)
                print_named_expr("init", d->var.init, depth + 1);

            indent(depth);
            printf(")\n");
            break;

        case DECL_FUNCTION:
            indent(depth);
            printf("(function %s\n", token_to_cstr(d->name));

            indent(depth + 1);
            printf("(type ");
            print_type_inline(d->ty);
            printf(")\n");

            print_decl_attrs(d, depth + 1);
            print_param_decls(d->func.params, depth + 1);

            if (d->func.body)
                print_named_stmt("body", d->func.body, depth + 1);
            else {
                indent(depth + 1);
                printf("(body nil)\n");
            }

            indent(depth);
            printf(")\n");
            break;
    }
}

static void print_decls(struct decl *decls, int depth)
{
    for (struct decl *d = decls; d; d = d->next)
        print_decl(d, depth);
}

static void print_for_init(struct for_init *init, int depth)
{
    if (!init) {
        indent(depth);
        printf("(init nil)\n");
        return;
    }

    if (init->is_decl) {
        indent(depth);
        printf("(init-decls\n");
        print_decls(init->decls, depth + 1);
        indent(depth);
        printf(")\n");
    } else {
        print_named_expr("init-expr", init->expr, depth);
    }
}

static void print_stmt(struct stmt *stmt, int depth)
{
    if (!stmt) {
        indent(depth);
        printf("nil\n");
        return;
    }

    switch (stmt->kind) {
        case STMT_NULL:
            indent(depth);
            printf("(null-stmt)\n");
            break;

        case STMT_EXPR:
            indent(depth);
            printf("(expr-stmt\n");
            print_expr(stmt->expr_stmt.expr, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case STMT_IF:
            indent(depth);
            printf("(if\n");
            print_named_expr("cond", stmt->if_stmt.condition, depth + 1);
            print_named_stmt("then", stmt->if_stmt.then_stmt, depth + 1);
            print_named_stmt("else", stmt->if_stmt.else_stmt, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case STMT_GOTO:
            indent(depth);
            printf("(goto %s)\n", token_to_cstr(stmt->goto_stmt.label));
            break;

        case STMT_LABEL:
            indent(depth);
            printf("(label %s\n", token_to_cstr(stmt->label_stmt.name));
            print_stmt(stmt->label_stmt.stmt, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case STMT_BREAK:
            indent(depth);
            if (stmt->break_stmt.target_label)
                printf("(break %s)\n", stmt->break_stmt.target_label);
            else
                printf("(break)\n");
            break;

        case STMT_CONTINUE:
            indent(depth);
            if (stmt->continue_stmt.target_label)
                printf("(continue %s)\n", stmt->continue_stmt.target_label);
            else
                printf("(continue)\n");
            break;

        case STMT_FOR:
            indent(depth);
            printf("(for\n");
            print_for_init(stmt->for_stmt.init, depth + 1);
            print_named_expr("cond", stmt->for_stmt.condition, depth + 1);
            print_named_expr("post", stmt->for_stmt.post, depth + 1);
            print_named_stmt("body", stmt->for_stmt.body, depth + 1);
            print_loop_labels(stmt->for_stmt.break_label,
                              stmt->for_stmt.continue_label,
                              depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case STMT_WHILE:
            indent(depth);
            printf("(while\n");
            print_named_expr("cond", stmt->while_stmt.condition, depth + 1);
            print_named_stmt("body", stmt->while_stmt.body, depth + 1);
            print_loop_labels(stmt->while_stmt.break_label,
                              stmt->while_stmt.continue_label,
                              depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case STMT_DOWHILE:
            indent(depth);
            printf("(do-while\n");
            print_named_stmt("body", stmt->dowhile_stmt.body, depth + 1);
            print_named_expr("cond", stmt->dowhile_stmt.condition, depth + 1);
            print_loop_labels(stmt->dowhile_stmt.break_label,
                              stmt->dowhile_stmt.continue_label,
                              depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case STMT_SWITCH:
            indent(depth);
            printf("(switch\n");
            print_named_expr("cond", stmt->switch_stmt.condition, depth + 1);
            print_named_stmt("body", stmt->switch_stmt.body, depth + 1);

            if (stmt->switch_stmt.break_label) {
                indent(depth + 1);
                printf("(break-label %s)\n", stmt->switch_stmt.break_label);
            }

            indent(depth);
            printf(")\n");
            break;

        case STMT_CASE:
            indent(depth);
            printf("(case\n");
            print_named_expr("value", stmt->case_stmt.value, depth + 1);

            if (stmt->case_stmt.label) {
                indent(depth + 1);
                printf("(label %s)\n", stmt->case_stmt.label);
            }

            print_block_items(stmt->case_stmt.items, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case STMT_DEFAULT:
            indent(depth);
            printf("(default\n");

            if (stmt->default_stmt.label) {
                indent(depth + 1);
                printf("(label %s)\n", stmt->default_stmt.label);
            }

            print_block_items(stmt->default_stmt.items, depth + 1);
            indent(depth);
            printf(")\n");
            break;

        case STMT_RETURN:
            indent(depth);
            printf("(return");

            if (!stmt->return_stmt.expr) {
                printf(")\n");
            } else {
                printf("\n");
                print_expr(stmt->return_stmt.expr, depth + 1);
                indent(depth);
                printf(")\n");
            }
            break;

        case STMT_BLOCK:
            indent(depth);
            printf("(block\n");
            print_block_items(stmt->block.items, depth + 1);
            indent(depth);
            printf(")\n");
            break;
    }
}

static void print_block_item(struct block_item *item, int depth)
{    
    if (!item) {
        indent(depth);
        printf("nil\n");
        return;
    }

    if (item->kind == BLOCK_ITEM_STMT)
        print_stmt(item->stmt, depth);
    else
        print_decls(item->decls, depth);
}


void ast_print(struct ast_program *program)
{
    printf("(program\n");

    if (!program || !program->decls) {
        printf(")\n");
        return;
    }

    print_decls(program->decls, 1);
    printf(")\n");
}
