#include <stdio.h>

#include "debug.h"
#include "lexer.h"

static const char *token_names[] = {
    #define X(name) [name] = #name,
    TOKEN_LIST
    #undef X
};

void debug_print_token(token_t token) {
    const char *name = token_names[token.type];

    if (name == NULL) {
        name = "UNKNOWN_TOKEN";
    }

    printf("%4d %-20s '%.*s'\n", 
           token.line, 
           name, 
           token.length, 
           token.start);
}
