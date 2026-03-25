#include <stdio.h>

#include "ast.h"

void ast_print(ast_node_t *node, int depth)
{
    if (!node) return;

#define INDENT() printf("%*s", depth * 2, "")

    switch (node->kind) {
        case AST_CONSTANT: 
            INDENT(); printf("(const %ld)\n", node->constant.value);
            break;
        case AST_IDENTIFIER:
            INDENT(); printf("(ident %.*s)\n", (int)node->token.length, node->token.start);
            break;
        case AST_EXPR_STMT:
            INDENT(); printf("(expr_stmt\n");
            ast_print(node->expr_stmt.expr, depth + 1);
            INDENT(); printf(")\n");
            break;
        case AST_RETURN:
            INDENT(); printf("(return_stmt\n");
            ast_print(node->return_stmt.expr, depth + 1);
            INDENT(); printf(")\n");
            break;
        case AST_UNARY:
            INDENT(); printf("(unary %.*s\n", (int)node->token.length, node->token.start);
            ast_print(node->unary.expr, depth + 1);
            INDENT(); printf(")\n");
            break;
        case AST_BLOCK:
            INDENT(); printf("(block\n");
            for (ast_node_t *n = node->block.first; n; n = n->next)
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
            for (ast_node_t *n = node->program.first; n; n = n->next)
                ast_print(n, depth + 1);
            INDENT(); printf(")\n");
            break;
    }

#undef INDENT
}
