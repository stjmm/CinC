#include "parser.h"
#include "lexer.h"

typedef struct {
    token current;
    token previous;

    diagnostics *diag;
    bool panic;
} parser;

static parser parser_state;

static void
parser_advance(void)
{
}

ast_program *
parse_translation_unit(
    const char *source,
    const char *filename,
    diagnostics *diag
)
{
    parser_state = (parser){
        .diag = diag
    };

    lexer_init(source, filename);
}
