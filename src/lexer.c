#include <string.h>
#include <stdbool.h>

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

static bool is_at_end(void)
{
    return *lexer.current == '\0';
}

static char advance(void)
{
    lexer.current++;
    return lexer.current[-1];
}

static char peek(void)
{
    return *lexer.current;
}

static char peek_next(void)
{
    if (is_at_end()) return '\0';
    return lexer.current[1];
}

static bool is_alpha(char c)
{
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
         c == '_') return true;
    return false;
}

static bool is_digit(char c)
{
    if (c >= '0' && c <= '9') return true;
    return false;
}

static token_t make_token(token_type_e type)
{
    token_t tok;
    tok.start = lexer.start;
    tok.length = lexer.current - lexer.start;
    tok.type = type;
    tok.line = lexer.line;
    return tok;
}

static void skip_whitespace(void)
{
    for (;;) {
        switch (peek()) {
            case ' ':
            case '\r':
            case '\t':
            case '\v':
            case '\f':
                advance();
                break;
            case '\n':
                advance();
                lexer.line++;
                break;
            default:
                return;
        }
    }
}

// For now we only take ints
static token_t number(void)
{
    while (is_digit(peek())) advance();
    return make_token(TOKEN_NUMBER);
}

static token_type_e check_keyword(unsigned int start, unsigned int length,
        const char *rest, token_type_e type)
{
    if (lexer.current - lexer.start == start + length &&
            memcmp(lexer.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static token_type_e identifier_type(void)
{
    switch (lexer.start[0]) {
        case 'i': return check_keyword(1, 2, "nt", TOKEN_INT);
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
    }
    return TOKEN_IDENTIFIER;
}

static token_t identifier(void)
{
    while (is_alpha(peek()) || is_digit(peek())) advance();
    return make_token(identifier_type());
}

token_t lexer_next_token()
{
    skip_whitespace();
    lexer.start = lexer.current;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();
    if (is_digit(c)) return number();
    if (is_alpha(c)) return identifier();

    switch (c) {
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_BRACE);
        case ';': return make_token(TOKEN_SEMICOLON);
    }

    return make_token(TOKEN_ERROR);
}
