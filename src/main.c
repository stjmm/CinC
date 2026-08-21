#include "lexer.h"

#include <stdio.h>

int main
(int argc, const char **argv)
{
    const char *prog = "int main(void)\n {\n int a = 5; \n}";
    lexer_init(prog, "foo.c");

    const char *token_names[] = {
#define X(token_name) #token_name,
        TOKEN_LIST
#undef X
    };

    token tok = lexer_next_token();
    while (tok.type != TOKEN_EOF) {
        printf("%s", token_names[tok.type]);
        printf(" %.*s\n", (int)tok.len, tok.start);
        tok = lexer_next_token();
    }
}
