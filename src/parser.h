#ifndef CINC_PARSER_H
#define CINC_PARSER_H

#include "ast.h"
#include "base/arena.h"

ast_node_t *parse_program(const char *source, arena_t *arena);

#endif
