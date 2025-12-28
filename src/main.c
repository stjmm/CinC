#include <stdio.h>

#include "parser.h"
#include "base/arena.h"
#include "ast.h"

int main(void)
{
    const char *program_source = "int main() {\n    return 2;\n return 0;\n}\n";
    // const char *program_source = "5 + 2 * 8;";

    arena_t *ast_arena = arena_create(1024 * 1024 * 10);
    
    ast_node_t *root_node = parse_program(program_source, ast_arena);
    ast_print(root_node, 0);

    arena_destroy(ast_arena);

    return 0;
}
