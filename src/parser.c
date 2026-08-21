#include "parser.h"
#include "lexer.h"

typedef struct {
    lexer lexer;

    token current;
    token previous;

    diagnostics *diag;
    bool panic;
} parser;

ast_program *
parse_translation_unit(
    const char *source,
    const char *filename,
    diagnostics *diag
)
{
    parser parser = {
        .diag = diag
    };

    lexer_init(source, filename);


}
