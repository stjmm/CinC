#include <stdio.h>

#include "ast.h"
#include "parser.h"

extern arena_t *ast_arena;

ast_node_t *ast_new_number(token_t token)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_NUMBER;
    node->token = token;
    node->number.value = 10;
    return node;
}

ast_node_t *ast_new_binary(token_t token, ast_node_t *left, ast_node_t *right)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_BINARY;
    node->token = token;
    node->binary.left = left;
    node->binary.right = right;
    return node;
}

ast_node_t *ast_new_unary(token_t token, ast_node_t *left)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_UNARY;
    node->token = token;
    node->unary.operand = left;
    return node;
}

static void print_indent(int indent)
{
    for (int i = 0; i < indent; i++) {
        printf(" ");
    }
}

static const char *token_type_to_str(token_type_e type)
{
    switch (type) {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        default: return "?";
    }
}

void ast_print(ast_node_t *node, int indent)
{
    if (!node) {
        print_indent(indent);
        printf("(null)\n");
    }

    print_indent(indent);

    switch (node->kind) {
        case AST_NUMBER:
            printf("AST_NUMBER %ld\n", node->number.value);
            break;
        case AST_BINARY:
            printf("AST_BINARY %s\n", token_type_to_str(node->token.type));
            ast_print(node->binary.left, indent + 1);
            ast_print(node->binary.right, indent + 1);
            break;
        case AST_UNARY:
            printf("AST_UNARY %s\n", token_type_to_str(node->token.type));
            ast_print(node->unary.operand, indent + 1);
            break;
        default:
            printf("UNKNOWN NODE KIND %d\n", node->kind);
            break;
    }
}
