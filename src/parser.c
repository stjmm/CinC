#include <stdbool.h>

#include "lexer.h"
#include "parser.h"

typedef struct {
    token_t previous;
    token_t current;
    bool has_error;
    bool panic_mode;
} parser_t;

parser_t parser;

static void advance(void)
{
    parser.previous = parser.current;
}

void parser_init(const char *source)
{

}
