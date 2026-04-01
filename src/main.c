#include <stdlib.h>

#include "parser.h"
#include "ir.h"
#include "x86.h"

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

    /* IR */
    ir_program_t *ir = tacky_build(root);

    /* CODEGEN */
    FILE *file = fopen("a.s", "w");
    emit_x86(ir, file);

    fclose(file);

    return 0;
}
