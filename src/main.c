#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "sema.h""
#include "ir.h"
#include "x86.h"

static char *read_file(const char *source)
{
    FILE *file = fopen(source, "r");
    if (!file)
        exit(1);
    
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char *buffer = malloc(file_size + 1);
    if (!buffer)
        exit(1);

    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    buffer[bytes_read] = '\0';
    
    fclose(file);
    return buffer;
}

static void parse_args(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s program.c\n", argv[0]);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    const char *program_source = read_file(argv[1]);

    /* LEXER PHASE */

    /* PARSER/AST PHASE */
    struct ast_node *root = parse_translation_unit(program_source);
    if (!root)
        exit(1);

    root = sema_analysis(root);
    if (!root)
        exit(1);

    ast_print(root, 0);
    //
    // /* IR */
    // struct ir_program *ir = build_tacky(root);
    //
    // /* CODEGEN */
    // FILE *file = fopen("a.s", "w");
    // emit_x86(ir, file);
    //
    // fclose(file);

    return 0;
}
