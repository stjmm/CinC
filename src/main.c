#include <stdlib.h>

#include "parser.h"
#include "arena.h"

int main(int argc, char **argv)
{
    const char *program_source = "int main(void) \n{ return !2; 3; }\n";
    arena_t *phase = arena_new(1024 * 1024);

    /* LEXER PHASE */

    /* PARSER/AST PHASE */
    ast_node_t *root = parse_program(program_source, phase);
    if (!root) {
        exit(1);
    }
    ast_print(root, 0);
}
