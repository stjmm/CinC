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

static char peek_next(void)
{
    if (is_at_end()) return '\0';
    return lexer_state.current[1];
}

static bool match(char expected)
{
    if (is_at_end())
        return false;
    if (*lexer_state.current != expected)
        return false;
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
    if (c >= '0' && c <= '9')
        return true;
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

/*
 * Skips block comments
 * Returns next token after block comment
 */
static struct token block_comment(void)
{
    while (!is_at_end()) {
        if (peek() == '*' && peek_next() == '/') {
            advance();
            advance();
            return lexer_next_token();
        }
        if (peek() == '\n') {
            lexer_state.line++;
            advance();
            lexer_state.line_start = lexer_state.current;
        } else {
            advance();
        }
    }

    return make_token(TOKEN_ERROR);
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
            case '/':
                if (peek_next() == '/')
                    while (peek() != '\n' && !is_at_end())
                        advance();
                else
                    return;
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

// Trie based keyword recognition
static enum token_type identifier_type(void)
{
    switch (lexer_state.start[0]) {
        case 'a': return check_keyword(1, 3, "uto", TOKEN_AUTO);
        case 'b': return check_keyword(1, 4, "reak", TOKEN_BREAK);
        case 'c': 
            if (lexer_state.current - lexer_state.start > 1)
                switch (lexer_state.start[1]) {
                    case 'a': return check_keyword(2, 2, "se", TOKEN_CASE);
                    case 'o': return check_keyword(2, 6, "ntinue", TOKEN_CONTINUE);
                }
            break;
        case 'd':
            if (lexer_state.current - lexer_state.start > 1)
                switch(lexer_state.start[1]) {
                    case 'e': return check_keyword(2, 5, "fault", TOKEN_DEFAULT);
                    case 'o': return check_keyword(2, 0, "", TOKEN_DO);
                }
            break;
        case 'e':
            if (lexer_state.current - lexer_state.start > 1)
                switch(lexer_state.start[1]) {
                    case 'l': return check_keyword(2, 2, "se", TOKEN_ELSE);
                    case 'x': return check_keyword(2, 4, "tern", TOKEN_EXTERN);
                }
            break;
        case 'f': return check_keyword(1, 2, "or", TOKEN_FOR);
        case 'g': return check_keyword(1, 3, "oto", TOKEN_GOTO);
        case 'i': 
            if (lexer_state.current - lexer_state.start > 1)
                switch (lexer_state.start[1]) {
                    case 'f': return check_keyword(2, 0, "", TOKEN_IF);
                    case 'n': return check_keyword(2, 1, "t", TOKEN_INT);
                }
            break;
        case 's': 
            if (lexer_state.current - lexer_state.start > 1)
                switch (lexer_state.start[1]) {
                    case 't': return check_keyword(2, 4, "atic", TOKEN_STATIC);
                    case 'w': return check_keyword(2, 4, "itch", TOKEN_SWITCH);
                }
            break;
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
        case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
        case 'v': return check_keyword(1, 3, "oid", TOKEN_VOID);
    }
    return TOKEN_IDENTIFIER;
}

static struct token identifier(void)
{
    while (is_alpha(peek()) || is_digit(peek()))
        advance();
    return make_token(identifier_type());
}

struct token lexer_next_token()
{
    skip_whitespace();
    lexer_state.start = lexer_state.current;

    if (is_at_end())
        return make_token(TOKEN_EOF);

    char c = advance();
    if (is_digit(c))
        return number();
    if (is_alpha(c))
        return identifier();

    switch (c) {
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_BRACE);
        case '+':
            if (match('+')) return make_token(TOKEN_PLUS_PLUS);
            else if (match('=')) return make_token(TOKEN_PLUS_EQUAL);
            else return make_token(TOKEN_PLUS);
        case '-':
            if (match('-')) return make_token(TOKEN_MINUS_MINUS);
            else if (match('=')) return make_token(TOKEN_MINUS_EQUAL);
            else return make_token(TOKEN_MINUS);
        case '*':
            if (match('=')) return make_token(TOKEN_STAR_EQUAL);
            else return make_token(TOKEN_STAR);
        case '/': 
            if (match('=')) return make_token(TOKEN_SLASH_EQUAL);
            else if (match('*')) return block_comment();
            else return make_token(TOKEN_SLASH);
        case '%': 
            if (match('=')) return make_token(TOKEN_PERCENT_EQUAL);
            else return make_token(TOKEN_PERCENT);
        case '~':
            return make_token(TOKEN_TILDE);
        case '=':
            if (match('=')) return make_token(TOKEN_EQUAL_EQUAL);
            else return make_token(TOKEN_EQUAL);
        case '!':
            if (match('=')) return make_token(TOKEN_BANG_EQUAL);
            else return make_token(TOKEN_BANG);
        case '&':
            if (match('&')) return make_token(TOKEN_AND_AND);
            else if (match('=')) return make_token(TOKEN_AND_EQUAL);
            else return make_token(TOKEN_AND);
        case '|':
            if (match('|')) return make_token(TOKEN_OR_OR);
            else if (match('=')) return make_token(TOKEN_OR_EQUAL);
            else return make_token(TOKEN_OR);
        case '^':
            if (match('=')) return make_token(TOKEN_CARET_EQUAL);
            else return make_token(TOKEN_CARET);
        case '<':
            if (match('=')) return make_token(TOKEN_LESS_EQUAL);
            else if (match('<')) {
                if (match('=')) return make_token(TOKEN_LESS_LESS_EQUAL);
                else return make_token(TOKEN_LESS_LESS);
            }
            else return make_token(TOKEN_LESS);
        case '>':
            if (match('=')) return make_token(TOKEN_GREATER_EQUAL);
            else if (match('>')) {
                if (match('=')) return make_token(TOKEN_GREATER_GREATER_EQUAL);
                return make_token(TOKEN_GREATER_GREATER);
            }
            else return make_token(TOKEN_GREATER);
        case ';': return make_token(TOKEN_SEMICOLON);
        case ':': return make_token(TOKEN_COLON);
        case '?': return make_token(TOKEN_QUESTION_MARK);
        case ',': return make_token(TOKEN_COMMA);
    }

    return make_token(TOKEN_ERROR);
}
