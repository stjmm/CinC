#include <stdbool.h>
#include <string.h>

#include "lexer.h"

typedef struct {
    const char *start;
    const char *current;
    int line;
} lexer_t;

lexer_t lexer;

void lexer_init(const char *source)
{
    lexer.start = source;
    lexer.current = source;
    lexer.line = 1;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c == '_');
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

static bool match(char expected)
{
    if (is_at_end()) return false;
    if (*lexer.current != expected) return false;
    lexer.current++;
    return true;
}

static token_t make_token(token_type_e type)
{
    token_t token;
    token.type = type;
    token.start = lexer.start;
    token.length = (int)(lexer.current - lexer.start);
    token.line = lexer.line;
    return token;
}

static token_t error_token(const char *message)
{
    token_t token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = strlen(message);
    token.line = lexer.line;
    return token;
}

static void skip_whitespace(void)
{
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\t':
            case '\r':
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

static token_type_e check_keyword(int start, int length,
                                  const char *rest, token_type_e type)
{
    if (lexer.current - lexer.start == start + length &&
        memcmp(lexer.start + start, rest, length) == 0)
        return type;
    
    return TOKEN_IDENTIFIER;
}

static token_type_e identifier_type(void)
{
    switch (*lexer.start) {
        case 'a': return check_keyword(1, 3, "uto", TOKEN_AUTO);
        case 'b': return check_keyword(1, 4, "reak", TOKEN_BREAK);
        case 'c': {
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'a': return check_keyword()
                }
            }            
        }
    }
}

static token_t identifier(void)
{

}

static token_t number(void)
{

}

static token_t string(void)
{

}

static token_t _char(void)
{

}

token_t lexer_scan_token(void)
{
    skip_whitespace();   
    lexer.start = lexer.current;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();
    if (is_alpha(c)) identifier();
    if (is_digit(c)) number();

    switch (c) {
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_PAREN);
        case '[': return make_token(TOKEN_RIGHT_BRACE);
        case ']': return make_token(TOKEN_LEFT_BRACE);
        case ',': return make_token(TOKEN_COMMA);
        case '.': return make_token(TOKEN_DOT);
        case ';': return make_token(TOKEN_SEMICOLON);
        case '?': return make_token(TOKEN_QUESTION);

        case '-': {
            if (match('-'))
                return make_token(TOKEN_MINUS_MINUS);
            else if (match('='))
                return make_token(TOKEN_MINUS_EQUAL);
            else if (match('>'))
                return make_token(TOKEN_ARROW);
            else
                return make_token(TOKEN_MINUS);
        }
        case '+': {
            if (match('+'))
                return make_token(TOKEN_PLUS_PLUS);
            else if (match('='))
                return make_token(TOKEN_PLUS_EQUAL);
            else
                return make_token(TOKEN_PLUS);
        }
        case '*': {
            if (match('='))
                return make_token(TOKEN_STAR_EQUAL);
            else
                return make_token(TOKEN_STAR);
        }
        case '/': {
            if (match('='))
                return make_token(TOKEN_SLASH_EQUAL);
            else
                return make_token(TOKEN_SLASH);
        }
        case '%': {
            if (match('='))
                return make_token(TOKEN_PERCENT_EQUAL);
            else
                return make_token(TOKEN_PERCENT);
        }
        case '&': {
            if (match('&'))
                return make_token(TOKEN_AMEPRSAND_AMPERSAND);
            else if (match('='))
                return make_token(TOKEN_AMPERSAND_EQUAL);
            else
                return make_token(TOKEN_AMEPRSAND);
        }
        case '|': {
            if (match('|'))
                return make_token(TOKEN_PIPE_PIPE);
            else if (match('='))
                return make_token(TOKEN_PIPE_EQUAL);
            else
                return make_token(TOKEN_PIPE);
        }
        case '^': {
            if (match('='))
                return make_token(TOKEN_CARET_EQUAL);
            else
                return make_token(TOKEN_CARET);
        }
        case '=': {
            if (match('='))
                return make_token(TOKEN_EQUAL_EQUAL);
            else
                return make_token(TOKEN_EQUAL);
        }
        case '!': {
            if (match('='))
                return make_token(TOKEN_BANG_EQUAL);
            else
                return make_token(TOKEN_BANG);
        }
        case '>': {
            if (match('='))
                return make_token(TOKEN_GREATER_EQUAL);
            else if (match('>'))
                if (match('='))
                    return make_token(TOKEN_GREATER_GREATER_EQUAL);
                else
                    return make_token(TOKEN_GREATER_GREATER);
            else
                return make_token(TOKEN_GREATER);
        }
        case '<': {
            if (match('='))
                return make_token(TOKEN_LESS_EQUAL);
            else if (match('<'))
                if (match('='))
                    return make_token(TOKEN_LESS_LESS_EQUAL);
                else
                    return make_token(TOKEN_LESS_LESS);
            else
                return make_token(TOKEN_LESS);
        }
    }

    return error_token("Unexpected character.");
}
