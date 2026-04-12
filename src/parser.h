#ifndef CINC_PARSER_H
#define CINC_PARSER_H

#include "ast.h"

struct ast_node *parse_translation_unit(const char *source);

#endif
