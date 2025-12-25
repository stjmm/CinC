#include <stdio.h>

#include "lexer.h"

static const char *token_names[] = {
#define X(name) [name] = #name,
    TOKEN_LIST
#undef X
};

int main(void)
{
    const char *program_source = "int main() {\n    return 0;\n}\n";
    lexer_init(program_source);

    token_t token = lexer_next_token();
    printf("%-20s | %-5s | %-5s\n", "TOKEN_TYPE", "TOKEN_LINE", "TOKEN_COLUMN");
    while (1) {
        printf("%-20s | %-10d | %-5d\n", token_names[token.type], token.line, token.column);
        if (token.type == TOKEN_EOF) break;
        token = lexer_next_token();
    }
    
    return 0;
}
