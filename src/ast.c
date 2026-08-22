#include "ast.h"
#include "base/memory.h"

ast_expr *
ast_new_expr(expr_kind kind, token tok)
{
    ast_expr *expr = xcalloc(1, sizeof(ast_expr));
    *expr = (ast_expr){
        .kind = kind,
        .tok = tok
    };

    return expr;
}

ast_stmt *
ast_stmt_new(stmt_kind kind, token tok)
{
    ast_stmt *stmt = xcalloc(1, sizeof(ast_stmt));
    *stmt = (ast_stmt){
        .kind = kind,
        .tok = tok
    };

    return stmt;
}

ast_decl *
ast_decl_new(decl_kind kind, token tok)
{
    ast_decl *decl = xcalloc(1, sizeof(ast_decl));
    *decl = (ast_decl){
        .kind = kind,
        .tok = tok
    };

    return decl;
}

ast_block_item *
ast_block_item_new(block_item_kind kind, token tok)
{
    ast_block_item *block_item = xcalloc(1, sizeof(ast_block_item));
    *block_item = (ast_block_item){
        .kind = kind,
        .tok = tok
    };

    return block_item;
}

ast_for_init *
ast_for_init_new(for_init_kind kind)
{
    ast_for_init *for_init = xcalloc(1, sizeof(ast_for_init));
    *for_init = (ast_for_init){
        .kind = kind
    };

    return for_init;
}

ast_program *
ast_program_new(void)
{
    ast_program *program = xcalloc(1, sizeof(ast_program));

    return program;
}
