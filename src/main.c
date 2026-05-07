#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "parser.h"
#include "sema.h"
#include "ir.h"
#include "x86.h"

static bool opt_c;
static bool opt_S;
static char *opt_o;

static char *input_files[64];
static int input_file_count = 0;

const char *current_filename;
bool had_error = false;

static char *read_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Opening file %s failed\n", filename);
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char *buffer = malloc(file_size + 1);
    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

static void run_cmd(const char *cmd)
{
    int status = system(cmd);
    if (status != 0) {
        fprintf(stderr, "Command failed: %s\n", cmd);
        exit(1);
    }
}

char *replace_ext(const char *path, const char *new_ext)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    const char *dot = strrchr(path, '.');

    size_t filename_len;
    if (dot)
        filename_len = (size_t)(dot - base);
    else
        filename_len = strlen(base);

    size_t new_size = filename_len + strlen(new_ext) + 1;
    char *new_name = malloc(new_size);
    
    memcpy(new_name, base, filename_len);
    strcpy(new_name + filename_len, new_ext);

    return new_name;
}

static void usage(const char *prog)
{
    fprintf(stderr, 
            "Usage: %s [options] <file1 file2...>\n"
            "Options:\n"
            "   -S          Stop after assembly (.s)\n"
            "   -c          Compile and assemble but don't link (.o)\n"
            "   -o <file>   Place the output into <file>\n"
            "Compiler Debug Options:\n"
            "   --lex       Debug: dump tokens\n"
            "   --parse     Debug: dump AST after parsing\n"
            "   --validate  Debug: dump AST after semantical analysis\n"
            "   --help      This message\n",
            prog);
    exit(1);
}

static void parse_args(int argc, char **argv)
{
    if (argc < 2)
        usage(argv[0]);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (!strcmp(arg, "--help")) {
            usage(argv[0]);
            continue;
        }

        if (!strcmp(arg, "-S")) {
            opt_S = true;
            continue;
        }

        if (!strcmp(arg, "-c")) {
            opt_c = true;
            continue;
        }

        if (!strcmp(arg, "-o")) {
            if (argc <= i + 1)
                usage(argv[0]);

            opt_o = argv[++i];
            continue;
        }

        input_files[input_file_count++] = argv[i];
    }

    if (input_file_count == 0)
        usage(argv[0]);

    if (opt_o && input_file_count > 1 && (opt_S || opt_c)) {
        usage(argv[0]);
    }
}

static bool compile_to_asm(const char *filename, const char *out_file)
{
    current_filename = filename;
    char *source = read_file(filename);

    struct ast_program *root = parse_translation_unit(source);
    if (!root) {
        had_error = true;
        return false;
    }

    root = sema_analysis(root);
    if (!root) {
        had_error = true;
        return false;
    }

    struct ir_program *program = build_ir(root);
    if (!program) {
        had_error = true;
        return false;
    }

    FILE *out_f = fopen(out_file, "w");
    emit_x86(program, out_f);

    fclose(out_f);
    free(source);
    return true;
}

static char *compile_file(const char *filename)
{
    if (opt_S) {
        char *asm_file = opt_o ? strdup(opt_o) : replace_ext(filename, ".s");

        if (!compile_to_asm(filename, asm_file)) {
            remove(asm_file);
            free(asm_file);
            return NULL;
        }

        return asm_file;
    }

    char *asm_file = replace_ext(filename, ".s");

    char *obj_file = (opt_c && opt_o)
        ? strdup(opt_o)
        : replace_ext(filename, ".o");

    if (!compile_to_asm(filename, asm_file)) {
        remove(asm_file);
        free(asm_file);
        free(obj_file);
        return NULL;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "cc -c %s -o %s", asm_file, obj_file);
    run_cmd(cmd);

    remove(asm_file);
    free(asm_file);

    return obj_file;
}

static void link_files(char **objects)
{
    const char *out = opt_o ? opt_o : "a.out";

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "cc");

    for (int i = 0; i < input_file_count; i++) {
        strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, objects[i], sizeof(cmd) - strlen(cmd) - 1);
    }

    strncat(cmd, " -o ", sizeof(cmd) - strlen(cmd) - 1);
    strncat(cmd, out, sizeof(cmd) - strlen(cmd) - 1);

    run_cmd(cmd);
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);

    char *objects[64];

    for (int i = 0; i < input_file_count; i++)
        objects[i] = compile_file(input_files[i]);

    if (had_error) {
        for (int i = 0; i < input_file_count; i++) { 
            if (objects[i])
                remove(objects[i]);
        }

        return 1;
    }

    if (!opt_S && !opt_c) {
        link_files(objects);
        for (int i = 0; i <  input_file_count; i++)
            remove(objects[i]);
    }

    return 0;
}
