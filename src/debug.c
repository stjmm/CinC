#include <stdio.h>

#include "ast.h"

static void print_expr(struct ast_node *node)
{
    if (!node) return;

    switch (node->type) {
        case AST_CONSTANT:
            printf("%ld", node->constant.value);
            break;
        case AST_IDENTIFIER:
            break;
        case AST_UNARY:
            printf("(%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->unary.expr);
            printf(")");
            break;
        case AST_BINARY:
            printf("(%.*s ", (int)node->token.length, node->token.start);
            print_expr(node->binary.left);
            printf(" ");
            print_expr(node->binary.right);
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
        case AST_EXPR_STMT:
            INDENT(); printf("(expr_stmt ");
            ast_print(node->expr_stmt.expr, depth + 1);
            printf(")\n");
            break;
        case AST_RETURN:
            INDENT(); printf("(return_stmt ");
            print_expr(node->return_stmt.expr);
            printf(")\n");
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
        case AST_BLOCK:
            INDENT(); printf("(block\n");
            for (struct ast_node *n = node->block.first; n; n = n->next)
                ast_print(n, depth + 1);
            INDENT(); printf(")\n");
            break;
        case AST_FUNCTION:
            INDENT(); printf("(fn %.*s -> %.*s\n",
                (int)node->function.name.length, node->function.name.start,
                (int)node->function.return_type.length, node->function.return_type.start);
            ast_print(node->function.body, depth + 1);
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
