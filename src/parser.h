#ifndef CINC_PARSER_H
#define CINC_PARSER_H

#include "ast.h"
#include "arena.h"

ast_node_t *parse_program(const char *source, arena_t *_phase);

#endif
