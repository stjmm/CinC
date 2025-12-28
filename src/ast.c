#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "base/arena.h"

extern arena_t *ast_arena;

ast_node_t *ast_new_number(token_t token)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_NUMBER;
    node->token = token;
    node->number.value = strtol(token.start, NULL, 10);
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

ast_node_t *ast_new_expr_stmt(ast_node_t *expr)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_EXPR_STMT;
    node->token = expr->token;
    node->expr_stmt.expr = expr;
    return node;
}

ast_node_t *ast_new_return(token_t token, ast_node_t *expr)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_RETURN;
    node->token = token;
    node->return_stmt.expr = expr;
    return node;
}

ast_node_t *ast_new_block(token_t token)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_BLOCK;
    node->token = token;
    node->block.count = 0;
    node->block.capacity = 8;
    node->block.stmts = ARENA_ALLOC_ARRAY(ast_arena, ast_node_t*, 8);
    return node;
}

void ast_block_append(ast_node_t *block, ast_node_t *stmt)
{
    if (block->block.count >= block->block.capacity) {
        unsigned int new_cap = block->block.capacity * 2;
        ast_node_t **new_stmts = ARENA_ALLOC_ARRAY(ast_arena, ast_node_t*, new_cap);
        memcpy(new_stmts, block->block.stmts, sizeof(ast_node_t*) * block->block.count);
        block->block.stmts = new_stmts;
        block->block.capacity = new_cap;
    }
    block->block.stmts[block->block.count++] = stmt;
}

ast_node_t *ast_new_function(token_t name, token_t return_type, ast_node_t *body)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_FUNCTION;
    node->token = name;
    node->function.name = name;
    node->function.return_type = return_type;
    node->function.body = body;
    return node;
}

ast_node_t *ast_new_program(void)
{
    ast_node_t *node = ARENA_ALLOC(ast_arena, ast_node_t);
    node->kind = AST_PROGRAM;
    node->program.capacity = 8;
    node->program.count = 0;
    node->program.decls = ARENA_ALLOC_ARRAY(ast_arena, ast_node_t*, 8);
    return node;
}

void ast_program_append(ast_node_t *program, ast_node_t *decl)
{
    if (program->program.count >= program->program.capacity) {
        int new_cap = program->program.capacity * 2;
        ast_node_t **new_decls = ARENA_ALLOC_ARRAY(ast_arena, ast_node_t*, new_cap);
        memcpy(new_decls, program->program.decls,
               sizeof(ast_node_t*) * program->program.count);
        program->program.decls = new_decls;
        program->program.capacity = new_cap;
    }
    program->program.decls[program->program.count++] = decl;
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
        case TOKEN_INT: return "int";
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
