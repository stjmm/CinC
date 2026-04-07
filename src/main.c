#include <stdlib.h>

#include "parser.h"
#include "ir.h"
#include "x86.h"

int main(int argc, char **argv)
{
    const char *program_source = "int main(void) \n{ return (4 >> 1); }\n";

    /* LEXER PHASE */

    /* PARSER/AST PHASE */
    struct ast_node *root = parse_program(program_source);
    if (!root) {
        exit(1);
    }
    ast_print(root, 0);

    /* IR */
    struct ir_program *ir = tacky_build(root);

    /* CODEGEN */
    FILE *file = fopen("a.s", "w");
    emit_x86(ir, file);

    fclose(file);

    return 0;
}
