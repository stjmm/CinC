/*
 * Recursive descent parser with Pratt expression parsing.
 * Produces an AST from flat token stream.
 * On error enters panic_mode and sychronizes to next statement boundary
 */

#ifndef CINC_PARSER_H
#define CINC_PARSER_H

#include "ast.h"

struct ast_program *parse_translation_unit(const char *source);

#endif
