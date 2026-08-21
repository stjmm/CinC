#ifndef CINC_PARSER_H
#define CINC_PARSER_H

#include "ast.h"
#include "diagnostics.h"

ast_program *parse_translation_unit(
    const char *source,
    const char *filename,
    diagnostics *diag);

#endif
