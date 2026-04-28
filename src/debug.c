#include <stdio.h>

#include "ast.h"

/*
 * Prints AST S-Expression style
 */

static void print_tok(struct token *tok)
{
    printf("%.*s", (int)tok->length, tok->start);
}

static void print_expr(struct expr *expr, int depth)
{
    if (!expr)
        return;

    switch (expr->kind) {
        case EXPR_INT_LITERAL:
            printf("%ld", expr->int_value);
            break;
        case EXPR_IDENTIFIER:
            print_tok(&expr->identifier.name);
            break;
        case EXPR_PRE:
        case EXPR_POST:
        case EXPR_UNARY:
            printf("(");
            print_tok(&expr->unary.op);
            printf(" ");
            print_expr(expr->unary.operand, depth + 1);
            printf(")");
            break;
        case EXPR_BINARY:
            printf("(");
    }
}

static void print_stmt(struct stmt *stmt)
{

}

static void print_decl(struct decl *decl)
{

}

static void print_expr(struct ast_node *node)
{
    if (!node) return;

    switch (node->type) {
        case AST_CONSTANT:
            printf("%ld", node->constant.value);
            break;
        case AST_IDENTIFIER:
            printf("%.*s", (int)node->token.length, node->token.start);
            break;
        case AST_UNARY:
            printf("(%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->unary.expr);
            printf(")");
            break;
        case AST_PRE:
            printf("(pre-%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->unary.expr);
            printf(")");
            break;
        case AST_POST:
            printf("(post-%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->unary.expr);
            printf(")");
            break;
        case AST_ASSIGNMENT:
            printf("(%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->assignment.lvalue);
            printf(" ");
            print_expr(node->assignment.rvalue);
            printf(")");
            break;
        case AST_BINARY:
            printf("(%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->binary.left);
            printf(" ");
            print_expr(node->binary.right);
            printf(")");
            break;
        case AST_TERNARY:
            printf("(? ");
            print_expr(node->ternary.condition);
            printf(" ");
            print_expr(node->ternary.then);
            printf(" ");
            print_expr(node->ternary.else_then);
            printf(")");
            break;
        default:
            printf("<expr?>");
            break;
    }
}

void ast_print(struct ast_node *node, int depth)
{
    if (!node) return;

#define INDENT() printf("%*s", depth * 2, "")

    switch (node->type) {
        case AST_CONSTANT: 
            INDENT(); printf("(const %ld)\n", node->constant.value);
            break;

        case AST_IDENTIFIER:
            INDENT(); printf("(ident %.*s)\n", (int)node->token.length, node->token.start);
            break;

        case AST_UNARY:
            INDENT(); printf("(%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->unary.expr);
            printf(")\n");
            break;

        case AST_BINARY:
            INDENT(); printf("(%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->binary.left);
            printf(" ");
            print_expr(node->binary.right);
            printf(")\n");
            break;

        case AST_PRE:
            INDENT(); printf("(pre-%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->unary.expr);
            printf(")\n");
            break;

        case AST_POST:
            INDENT(); printf("(post-%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->unary.expr);
            printf(")\n");
            break;

        case AST_ASSIGNMENT:
            INDENT(); printf("(= ");
            print_expr(node->assignment.lvalue);
            printf(" ");
            print_expr(node->assignment.rvalue);
            printf(")\n");
            break;;

        case AST_TERNARY:
            INDENT(); printf("(?\n");
            ast_print(node->ternary.condition, depth + 1);
            ast_print(node->ternary.then,      depth + 1);
            ast_print(node->ternary.else_then, depth + 1);
            INDENT(); printf(")\n");
            break;

        case AST_NULL_STMT:
            INDENT(); printf("(null_stmt)\n");
            break;

        case AST_EXPR_STMT:
            INDENT(); printf("(expr_stmt ");
            print_expr(node->expr_stmt.expr);
            printf(")\n");
            break;

        case AST_RETURN:
            INDENT(); printf("(return_stmt ");
            print_expr(node->return_stmt.expr);
            printf(")\n");
            break;

        case AST_IF_STMT:
            INDENT(); printf("(if\n");
            INDENT(); printf("  (cond ");
            print_expr(node->if_stmt.condition);
            printf(")\n");
            ast_print(node->if_stmt.then, depth + 1);
            if (node->if_stmt.else_then) {
                INDENT(); printf("  (else\n");
                ast_print(node->if_stmt.else_then, depth + 2);
                INDENT(); printf("  )\n");
            }
            INDENT(); printf(")\n");
            break;

       case AST_GOTO:
            INDENT(); printf("(goto %.*s)\n",
                (int)node->goto_stmt.label.length, node->goto_stmt.label.start);
            break;

        case AST_LABEL_STMT:
            INDENT(); printf("(label %.*s)\n",
                (int)node->label_stmt.name.length, node->label_stmt.name.start);
            break;

        case AST_BREAK:
            INDENT();
            if (node->break_stmt.target_label)
                printf("(break -> %s)\n", node->break_stmt.target_label);
            else
                printf("(break)\n");
            break;

        case AST_CONTINUE:
            INDENT();
            if (node->continue_stmt.target_label)
                printf("(continue -> %s)\n", node->continue_stmt.target_label);
            else
                printf("(continue)\n");
            break;

        case AST_WHILE:
            INDENT();
            if (node->while_stmt.label)
                printf("(while [%s]\n", node->while_stmt.label);
            else
                printf("(while\n");
            INDENT(); printf("  (cond ");
            print_expr(node->while_stmt.condition);
            printf(")\n");
            ast_print(node->while_stmt.body, depth + 1);
            INDENT(); printf(")\n");
            break;

        case AST_DOWHILE:
            INDENT();
            if (node->do_while.label)
                printf("(do-while [%s]\n", node->do_while.label);
            else
                printf("(do-while\n");
            ast_print(node->do_while.body, depth + 1);
            INDENT(); printf("  (cond ");
            print_expr(node->do_while.condition);
            printf(")\n");
            INDENT(); printf(")\n");
            break;

        case AST_FOR:
            INDENT();
            if (node->for_stmt.label)
                printf("(for [%s]\n", node->for_stmt.label);
            else
                printf("(for\n");
            if (node->for_stmt.for_init) {
                ast_print(node->for_stmt.for_init, depth + 1);
            } else {
                INDENT(); printf("  (no-init)\n");
            }
            INDENT(); printf("  (cond ");
            if (node->for_stmt.condition)
                print_expr(node->for_stmt.condition);
            else
                printf("1");
            printf(")\n");
            INDENT(); printf("  (post ");
            if (node->for_stmt.post)
                print_expr(node->for_stmt.post);
            else
                printf("_");
            printf(")\n");
            ast_print(node->for_stmt.body, depth + 1);
            INDENT(); printf(")\n");
            break;

        case AST_SWITCH:
            INDENT();
            if (node->case_stmt.label)
                printf("(switch [%s]\n", node->switch_stmt.label);
            else
                printf("(switch\n");
            INDENT(); printf("  (cond ");
            print_expr(node->switch_stmt.condition);
            printf("\n");
            ast_print(node->switch_stmt.body, depth + 1);
            INDENT(); printf(")\n");
            break;

        case AST_CASE:
            INDENT();
            if (node->case_stmt.label)
                printf("(case [%s] ", node->case_stmt.label);
            else
                printf("(case ");
            print_expr(node->case_stmt.value);
            printf("\n");
            for (struct ast_node *n = node->case_stmt.first; n; n = n->next)
                ast_print(n, depth + 1);
            INDENT(); printf(")\n");
            break;

        case AST_DEFAULT:
            INDENT();
            if (node->default_stmt.label)
                printf("(default [%s]\n", node->default_stmt.label);
            else
                printf("(default\n");
            for (struct ast_node *n = node->default_stmt.first; n; n = n->next)
                ast_print(n, depth + 1);
            INDENT(); printf(")\n");
            break;

        case AST_VAR_DECL:
            INDENT(); printf("(decl %.*s", (int)node->token.length, node->token.start);
            if (node->var_decl.init) {
                printf(" = ");
                print_expr(node->var_decl.init);
            }
            printf(")\n");
            break;

        case AST_BLOCK:
            INDENT(); printf("(block\n");
            for (struct ast_node *n = node->block.first; n; n = n->next)
                ast_print(n, depth + 1);
            INDENT(); printf(")\n");
            break;

        case AST_FUN_DECL:
            INDENT(); printf("(fn %.*s -> %.*s\n",
                (int)node->token.length, node->token.start,
                (int)node->fun_decl.return_type.length, node->fun_decl.return_type.start);
            ast_print(node->fun_decl.body, depth + 1);
            INDENT(); printf(")\n");
            break;

        case AST_PROGRAM:
            INDENT(); printf("(program\n");
            for (struct ast_node *n = node->program.first; n; n = n->next)
                ast_print(n, depth + 1);
            INDENT(); printf(")\n");
            break;
    }

#undef INDENT
}
