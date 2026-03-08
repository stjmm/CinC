#include "parser.h"
#include "arena.h"

int main(int argc, char **argv)
{
    const char *program_source = { "int main(void) \n{ return 2; }\n"};
    arena_t *phase = arena_new(1024 * 1024);
    arena_t *perm = arena_new(1024 * 1024);

    /* LEXER PHASE */

    /* PARSER/AST PHASE */
    ast_node_t *root = parse_program(program_source, phase);
}
