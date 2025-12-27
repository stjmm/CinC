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
