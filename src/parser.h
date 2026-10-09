#ifndef CINC_PARSER_H
#define CINC_PARSER_H

#include "ast.h"

ast_program_t *parse_translation_unit(
    const char *source,
    const char *filename);

#endif
