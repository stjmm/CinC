#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "parser.h"
#include "sema.h"
#include "ir.h"
#include "x86.h"

#define MAX_INPUTS 256

typedef enum {
    STAGE_ASM,  // -S
    STAGE_OBJ,  // -c
    STAGE_LINK  // produce executable
} stage;

typedef enum {
    DEBUG_NONE,
    DEBUG_LEX,
    DEBUG_PARSE,
    DEBUG_VALIDATE,
    DEBUG_TACKY
} debug;

static debug dbg = DEBUG_NONE;

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

static char *replace_ext(const char *path, const char *ext)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t stem = dot ? (size_t)(dot - base) : strlen(base);
    size_t elen = ext ? strlen(ext) : 0;
    char *out = malloc(stem + elen + 1);
    memcpy(out, base, stem);
    if (ext) memcpy(out + stem, ext, elen);
    out[stem + elen] = '\0';
    return out;
}

static void run(const char *cmd)
{
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed: %s\n", cmd);
        exit(1);
    }
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
            "   --parse     Debug: dump AST\n"
            "   --validate  Debug: dump AST after semantical analysis\n"
            "   --tacky     Debug: stop after IR\n"
            "   --help      This message\n",
            prog);
    exit(1);
}

static char *token_strings[] = {
#define X(TOKEN_NAME) [TOKEN_NAME] = #TOKEN_NAME,
    TOKEN_LIST
#undef X
};

static char *compile_one(stage stg, const char *src, const char *out_hint)
{
    char *source = read_file(src);

    if (dbg == DEBUG_LEX) {
        struct token tok = lexer_next_token();
        while (tok.type != TOKEN_EOF) {
            if (tok.type == TOKEN_ERROR)
                exit(1);

            printf("%s\n", token_strings[tok.type]);
            tok = lexer_next_token();
        }
        exit(0);
    }

    struct ast_node *root = parse_translation_unit(source);
    if (!root)
        exit(1);

    if (dbg == DEBUG_PARSE) {
        ast_print(root, 0);
        exit(0);
    }

    root = sema_analysis(root);
    if (!root)
        exit(1);

    if (dbg == DEBUG_VALIDATE) {
        ast_print(root, 0);
        exit(0);
    }

    struct ir_program *ir = build_tacky(root);

    char *asm_path = (out_hint && stg == STAGE_ASM) ? replace_ext(out_hint, NULL) : replace_ext(src, ".s");
    FILE *file = fopen(asm_path, "w");
    if (!file)
        exit(1);
    emit_x86(ir, file);
    fclose(file);

    if (stg == STAGE_ASM)
        return asm_path;

    char *obj_path = (out_hint && stg == STAGE_OBJ) ? replace_ext(out_hint, NULL) : replace_ext(src, ".o");
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "gcc -c %s -o %s", asm_path, obj_path);
    run(cmd);
    free(asm_path);
    return obj_path;
}


int main(int argc, char **argv)
{
    if (argc < 2)
        usage(argv[0]);

    stage stg = STAGE_LINK;
    const char *output = NULL;
    const char *inputs[MAX_INPUTS];
    int input_count = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-S"))
            stg    = STAGE_ASM;
        else if (!strcmp(argv[i], "-c"))
            stg    = STAGE_OBJ;
        else if (!strcmp(argv[i], "-o"))
            output   = argv[++i];
        else if (!strcmp(argv[i], "--lex"))
            dbg  = DEBUG_LEX;
        else if (!strcmp(argv[i], "--parse"))
            dbg  = DEBUG_PARSE;
        else if (!strcmp(argv[i], "--validate"))
            dbg  = DEBUG_VALIDATE;
        else if (!strcmp(argv[i], "--tacky"))
            dbg  = DEBUG_TACKY;
        else if (argv[i][0] == '-')
            usage(argv[0]);
        else
            inputs[input_count++] = argv[i];
    }

    if (input_count == 0)
        usage(argv[0]);
    if (output && stg == STAGE_OBJ && input_count > 1) {
        fprintf(stderr, "Can't use -c and -o with multiple input files");
        exit(1);
    }

    char *objs[MAX_INPUTS];
    int obj_count = 0;

    for (int i = 0; i < input_count; i++) {
        const char *hint = (input_count == 1) ? output : NULL;
        char *r = compile_one(stg, inputs[i], hint);
        if (r) objs[obj_count++] = r;
    }
 
    if (stg == STAGE_LINK && dbg == DEBUG_NONE && obj_count > 0) {
        const char *out = output ? output : "a.out";
        char cmd[8192];
        int pos = snprintf(cmd, sizeof(cmd), "gcc");
        for (int i = 0; i < obj_count; i++)
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s", objs[i]);
        snprintf(cmd + pos, sizeof(cmd) - pos, " -o %s", out);
        run(cmd);
    }
 
    for (int i = 0; i < obj_count; i++) free(objs[i]);
    return 0;
}
