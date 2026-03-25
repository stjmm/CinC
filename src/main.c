#include <stdio.h>
#include <stdlib.h>

#include "parser.h"

int main(int argc, char **argv)
{
    const char *program_source = "int main(void) \n{ return ~(-2); }\n";

    /* LEXER PHASE */

    /* PARSER/AST PHASE */
    ast_node_t *root = parse_program(program_source);
    if (!root) {
        exit(1);
    }
    ast_print(root, 0);
}
