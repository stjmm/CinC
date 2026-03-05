#include <stdio.h>

#include "lexer.h"

int main(int argc, char **argv)
{
    const char *program_source = { "int main(void) \n{ return 2; }\n"};

    lexer_init(program_source);
    token_t tok = lexer_next_token();
    while (tok.type != TOKEN_EOF) {
        const char *tok_str = token_name_strings[tok.type];
        printf("%s: %.*s (line %u, col %u)\n",
               tok_str,
               (int)tok.length,
               tok.start,
               tok.line,
               tok.column);
        tok = lexer_next_token();
    }
}
