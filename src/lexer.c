#include <string.h>
#include <stdbool.h>

#include "lexer.h"

struct lexer {
    const char *start;
    const char *current;
    const char *line_start;
    int line;
};

static struct lexer lexer_state;

void lexer_init(const char *source)
{
    lexer_state.start = source;
    lexer_state.current = source;
    lexer_state.line_start = source;
    lexer_state.line = 1;
}

static bool is_at_end(void)
{
    return *lexer_state.current == '\0';
}

static char advance(void)
{
    lexer_state.current++;
    return lexer_state.current[-1];
}

static char peek(void)
{
    return *lexer_state.current;
}

static bool match(char expected)
{
    if (is_at_end()) return false;
    if (*lexer_state.current != expected) return false;
    lexer_state.current++;
    return true;
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

static struct token make_token(enum token_type type)
{
    struct token tok;
    tok.start = lexer_state.start;
    tok.length = lexer_state.current - lexer_state.start;
    tok.type = type;
    tok.line = lexer_state.line;
    tok.line_start = lexer_state.line_start;
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
                lexer_state.line++;
                lexer_state.line_start = lexer_state.current;
                break;
            default:
                return;
        }
    }
}

// For now we only take ints
static struct token number(void)
{
    while (is_digit(peek())) advance();
    return make_token(TOKEN_NUMBER);
}

static enum token_type check_keyword(unsigned int start, unsigned int length,
        const char *rest, enum token_type type)
{
    if (lexer_state.current - lexer_state.start == start + length &&
            memcmp(lexer_state.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static enum token_type identifier_type(void)
{
    switch (lexer_state.start[0]) {
        case 'i': return check_keyword(1, 2, "nt", TOKEN_INT);
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
    }
    return TOKEN_IDENTIFIER;
}

static struct token identifier(void)
{
    while (is_alpha(peek()) || is_digit(peek())) advance();
    return make_token(identifier_type());
}

struct token lexer_next_token()
{
    skip_whitespace();
    lexer_state.start = lexer_state.current;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();
    if (is_digit(c)) return number();
    if (is_alpha(c)) return identifier();

    switch (c) {
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_BRACE);
        case '+':
            if (match('+')) return make_token(TOKEN_PLUS_PLUS);
            else return make_token(TOKEN_PLUS);
        case '-':
            if (match('-')) return make_token(TOKEN_MINUS_MINUS);
            else return make_token(TOKEN_MINUS);
        case '*': return make_token(TOKEN_STAR);
        case '/': return make_token(TOKEN_SLASH);
        case '%': return make_token(TOKEN_PERCENT);
        case '~': return make_token(TOKEN_TILDE);
        case '=':
            if (match('=')) return make_token(TOKEN_EQUAL_EQUAL);
            else return make_token(TOKEN_EQUAL);
        case '!':
            if (match('=')) return make_token(TOKEN_BANG_EQUAL);
            else return make_token(TOKEN_BANG);
        case '&':
            if (match('&')) return make_token(TOKEN_AND_AND);
            else return make_token(TOKEN_AND);
        case '|':
            if (match('|')) return make_token(TOKEN_OR_OR);
            else return make_token(TOKEN_OR);
        case '<':
            if (match('=')) return make_token(TOKEN_LESS_EQUAL);
            else if (match('<')) return make_token(TOKEN_LESS_LESS);
            else return make_token(TOKEN_LESS);
        case '>':
            if (match('=')) return make_token(TOKEN_GREATER_EQUAL);
            else if (match('>')) return make_token(TOKEN_GREATER_GREATER);
            else return make_token(TOKEN_GREATER);
        case ';': return make_token(TOKEN_SEMICOLON);
    }

    return make_token(TOKEN_ERROR);
}
