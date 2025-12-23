#include <string.h>
#include <stdbool.h>
#include <stdio.h>

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

static token_t make_token(token_type_e type)
{
    token_t token;
    token.type = type;
    token.start = lexer.start;
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

static bool is_digit(char c)
{
    if (c >= '0' && c <= '9') return true;
    return false;
}

static bool is_alphanumeric(char c)
{
    if (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c == '_') return true;
    return false;
}

static void skip_whitespace(void)
{
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                lexer.line++;
                advance();
                break;
            default:
                return;
        }
    }
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
    while (is_alphanumeric(peek()) || is_digit(peek())) advance();
    return make_token(identifier_type());
}

static token_t number(void)
{
    while (is_digit(peek())) advance();
    return make_token(TOKEN_NUMBER);
}

token_t lexer_next_token(void)
{
    skip_whitespace();
    lexer.start = lexer.current;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();
    if (is_digit(c)) return number();
    if (is_alphanumeric(c)) return identifier();

    switch (c) {
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_BRACE);
        // case '[': return make_token(TOKEN_LEFT_BRACKET);
        // case ']': return make_token(TOKEN_RIGHT_BRACKET);
        case ';': return make_token(TOKEN_SEMICOLON);
        case '+': return make_token(TOKEN_PLUS);
        case '-': return make_token(TOKEN_MINUS);
        case '*': return make_token(TOKEN_STAR);
        case '/': return make_token(TOKEN_SLASH);
    }

    return make_error_token("Unexpected character.");
}

const char *token_names[] = {
#define X(name) [name] = #name,
    TOKEN_LIST
#undef X
};

void lexer_print_all(const char *source)
{
    lexer_init(source);
    token_t token = lexer_next_token();
    while (token.type != TOKEN_EOF) {
        printf("%s\n", token_names[token.type]);
        token = lexer_next_token();
    }
}
