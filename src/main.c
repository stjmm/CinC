#include "diagnostics.h"
#include "parser.h"
#include "sema.h"
#include "ir.h"
#include "x86.h"
#include "base/memory.h"
#include "base/vector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool opt_c;
static bool opt_S;
static const char *output_file;

static vector input_files;

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
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "error: cannot open '%s'\n", filename);
        exit(EXIT_FAILURE);
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if (file_size < 0) {
        fprintf(stderr, "error: cannot read '%s'\n", filename);
        exit(EXIT_FAILURE);
    }
    rewind(file);

    char *buffer = xmalloc((size_t)file_size + 1);
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

static void
run_command(const char *cmd)
{
    int status = system(cmd);
    if (status != 0) {
        fatal("error: command failed");
    }
}

static char *
replace_ext(const char *path, const char *new_ext)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    const char *dot = strrchr(base, '.');

    size_t filename_len = dot ? (size_t)(dot - base) : strlen(base);

    char *new_name = xmalloc(filename_len + strlen(new_ext) + 1);

    memcpy(new_name, base, filename_len);
    strcpy(new_name + filename_len, new_ext);

    return new_name;
}

static char *
xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    return memcpy(xmalloc(len), s, len);
}

static bool
compile_to_asm(const char *filename, const char *out_file)
{
    char *source = read_file(filename);

    ast_program_t *program = parse_translation_unit(source, filename);
    if (!program)
        return false;

    sema_result_t result;
    if (!sema_analyze(&result, program))
        return false;

    ir_program_t *ir_program = ir_build(&result);

    FILE *out = fopen(out_file, "w+");

    if (!asm_emit(ir_program, &result, out))
        return false;

    fclose(out);

    return true;
}

static char *
compile_file(const char *filename)
{
    if (opt_S) {
        char *asm_file = output_file
            ? xstrdup(output_file)
            : replace_ext(filename, ".s");

        if (!compile_to_asm(filename, asm_file)) {
            remove(asm_file);
            free(asm_file);
            return nullptr;
        }

        return asm_file;
    }

    char *asm_file = replace_ext(filename, ".s");

    char *obj_file = (opt_c && output_file)
        ? xstrdup(output_file)
        : replace_ext(filename, ".o");

    if (!compile_to_asm(filename, asm_file)) {
        remove(asm_file);
        free(asm_file);
        free(obj_file);
        return nullptr;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "cc -c %s -o %s", asm_file, obj_file);
    run_command(cmd);

    remove(asm_file);
    free(asm_file);

    return obj_file;
}

static void
link_files(const vector *objects)
{
    const char *out = output_file ? output_file : "a.out";

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "cc");

    for (size_t i = 0; i < objects->count; i++) {
        strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, *VECTOR_GET(objects, char *, i),
                sizeof(cmd) - strlen(cmd) - 1);
    }

    strncat(cmd, " -o ", sizeof(cmd) - strlen(cmd) - 1);
    strncat(cmd, out, sizeof(cmd) - strlen(cmd) - 1);

    run_command(cmd);
}

static void
parse_args(int argc, char **argv)
{
    VECTOR_INIT(&input_files, const char *);

    if (argc < 2)
        usage();

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (!strcmp(arg, "--help"))
            usage();

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
            }

            output_file = argv[++i];
            continue;
        }

        vector_push(&input_files, &arg);
    }

    if (input_files.count == 0)
        fatal("error: no input files");

    if (opt_S && opt_c)
        fatal("error: cannot use '-c' and '-S' together");

    if (output_file && input_files.count > 1 && (opt_S || opt_c)) {
        fatal("error: cannot use '-o' with multiple input files "
              "when using '-c' or '-S'");
    }
}

int
main(int argc, char **argv)
{
    parse_args(argc, argv);
    diagnostics_init(stderr);

    vector objects;
    VECTOR_INIT(&objects, char *);

    bool failed = false;

    for (size_t i = 0; i < input_files.count; i++) {
        const char *filename = *VECTOR_GET(&input_files, const char *, i);
        char *out = compile_file(filename);

        if (!out) {
            failed = true;
            continue;
        }

        vector_push(&objects, &out);
    }

    if (!failed && !opt_c && !opt_S)
        link_files(&objects);

    // Linking consumed the temporary objects; -c keeps them
    for (size_t i = 0; i < objects.count; i++) {
        char *obj = *VECTOR_GET(&objects, char *, i);

        if (!opt_c && !opt_S)
            remove(obj);

        free(obj);
    }

    vector_free(&objects);
    vector_free(&input_files);

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
