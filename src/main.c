#include <stdlib.h>

#include "cinc.h"
#include "parser.h"
#include "codegen.h"
#include "base/arena.h"

int main(void)
{
    const char *program_source = "int main() {\n    return ((2  + 3) *5);\n return 0;\n}";
    // const char *program_source = "5 + 2 * 8;";

    arena_t *ast_arena = arena_create(1024 * 1024);
    ast_node_t *root_node = parse_program(program_source, ast_arena);
    if (!root_node) {
        exit(1);
    }

#ifdef CINC_DEBUG_PRINT_AST
    ast_print(root_node, 0);
#endif

    arena_t *ir_arena = arena_create(1024 * 1024);

    arena_destroy(ir_arena);
    arena_destroy(ast_arena);
    return 0;
}

