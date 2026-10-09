#include "ast.h"
#include "base/memory.h"

ast_expr_t *
ast_expr_new(expr_kind kind, token_t tok)
{
    ast_expr_t *expr = xcalloc(1, sizeof(ast_expr_t));
    *expr = (ast_expr_t){
        .kind = kind,
        .tok = tok
    };

    return expr;
}

ast_stmt_t *
ast_stmt_new(stmt_kind kind, token_t tok)
{
    static uint32_t next_stmt_id;

    ast_stmt_t *stmt = xcalloc(1, sizeof(ast_stmt_t));
    *stmt = (ast_stmt_t){
        .kind = kind,
        .tok = tok,
        .id = next_stmt_id++
    };

    return stmt;
}

ast_decl_t *
ast_decl_new(decl_kind kind, token_t tok)
{
    ast_decl_t *decl = xcalloc(1, sizeof(ast_decl_t));
    *decl = (ast_decl_t){
        .kind = kind,
        .name = tok
    };

    return decl;
}

ast_program_t *
ast_program_new(void)
{
    ast_program_t *program = xcalloc(1, sizeof(ast_program_t));
    return program;
}
