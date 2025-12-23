#include <string.h>

#include "lexer.h"

typedef struct {
    const char *start;
    const char *current;
    unsigned int line;
} lexer_t;

static lexer_t lexer;

void lexer_init(const char *source)
{
    lexer.start = source;
    lexer.current = source;
    lexer.line = 1;
}

static token_t make_token(token_type_e type)
{
    token_t token;
    token.type = type;
    token.start = lexer.current;
    token.length = lexer.current - lexer.start;
    token.line = lexer.line;
    
    return token;
}

static token_t make_error_token(const char *message)
{
    token_t token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = strlen(message);
    token.line = lexer.line;
    
    return token;
}

token_t lexer_next_token(void)
{
    
}
