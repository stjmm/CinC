#include "lexer.h"
#include "diagnostics.h"
#include "base/vector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool opt_c;
static bool opt_S;
static char *output_file;

static vector input_files;

static const char *current_filename;

static void
fatal(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static void
usage(void)
{
    fprintf(stderr, 
            "Usage: cinc [options] <file1 file2...>\n"
            "Options:\n"
            "   -S          Stop after assembly (.s)\n"
            "   -c          Compile and assemble but don't link (.o)\n"
            "   -o <file>   Place the output into <file>\n");
    exit(EXIT_FAILURE);
}

static char *
read_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file) {
        fatal("error: opening a file failed");
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

static void
run_command(const char *cmd)
{
    int status = system(cmd);
    if (status != 0) {
        fatal("error: command failed\n");
    }
}

char *
replace_ext(const char *path, const char *new_ext)
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

static void
parse_args(int argc, char **argv)
{
    if (argc < 2)
        usage();

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (!strcmp(arg, "--help")) {
            usage();
            continue;
        }

        if (!strcmp(arg, "-c")) {
            opt_c = true;
            continue;
        }

        if (!strcmp(arg, "-S")) {
            opt_S = true;
            continue;
        }

        if (!strcmp(arg, "-o")) {
            if (argc <= i + 1) {
                fprintf(stderr, "error: '-o' requires a filename\n");
                usage();
                exit(EXIT_FAILURE);
            }

            output_file = argv[++i];
            continue;
        }

        if(!vector_push(&input_files, &arg)) {
            fatal("error: out of memory\n");
        }
    }

    if (input_files.count == 0){
        fatal("error: no input files\n");
    }

    if (opt_S && opt_c) {
        fatal("error: cannot use '-c' and '-S' together\n");
    }

    if (output_file && input_files.count > 1 &&
        (opt_S && opt_c)) {
        fatal("error: cannot use '-o' with multiple input files"
                "when using '-c' or '-S'\n");
    }
}

int main
(int argc, char **argv)
{
    parse_args(argc, argv);



    return 0;
}
