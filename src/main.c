#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    const char *file_path;
    bool lex_only;
    bool parse_only;
    bool codegen_only;
    bool emit_asm;
} compiler_config_t;

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open the file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char *buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    if (bytes_read < file_size) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

static void run_command(const char *cmd)
{
    printf("[RUN] %s\n", cmd);
    int result = system(cmd);
    if (result != 0) {
        fprintf(stderr, "Command failed: %s\n", cmd);
        exit(1);
    }
}

static void preprocess(const char *input, const char *output)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "gcc -E -P %s -o %s", input, output);
    run_command(cmd);
}

static void assemble_and_link(const char *asm_file, const char *output)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "gcc %s -o %s", asm_file, output);
    run_command(cmd);
}

int main(int argc, char **argv)
{
    compiler_config_t config = {
        .file_path = NULL,
        .lex_only = false,
        .parse_only = false,
        .codegen_only = false,
        .emit_asm = false
    };

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: cinc [--lex | --parse | --codegen | -S] <file.c>\n");
        exit(64);
    } else if (argc == 2) {
        config.file_path = argv[1];
    } else if (argc == 3) {
        config.file_path = argv[2];
        if (strcmp(argv[1], "--lex") == 0)
            config.lex_only = true;
        else if (strcmp(argv[1], "--parse") == 0)
            config.parse_only = true;
        else if (strcmp(argv[1], "--codegen") == 0)
            config.codegen_only = true;
        else if (strcmp(argv[1], "--S") == 0)
            config.emit_asm = true;
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[1]);
            exit(64);
        }
    }

    const char *input_file = read_file(config.file_path);
    const char *pre_file = "temp.i";
    const char *asm_file = "temp.s";
    const char *output_file = "temp.out";

    preprocess(input_file, pre_file);

    if (config.lex_only) {
        printf("Lexing only: %s\n", config.file_path);
        // lexing
    } else if (config.parse_only) {
        printf("Lexing and parsing: %s\n", config.file_path);
        // parser
    } else if (config.codegen_only) {
        printf("Lexing, parsing and codegen: %s\n", config.file_path);
        // parser
    } else if (config.emit_asm) {
        printf("Emit assembly: %s\n", config.file_path);
        // parser
    } else {
        printf("Full compile: %s\n", config.file_path);
        // lexing + parsing + codegen
        assemble_and_link(asm_file, output_file);
    }

    free((void*)input_file);
}
