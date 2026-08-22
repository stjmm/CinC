#include "lexer.h"

#include <string.h>

typedef struct {
    const char *name;
    size_t len;
    token_type type;
} keyword;

static const keyword KEYWORDS[] = {
    {"auto",     4, TOKEN_AUTO},
    {"break",    5, TOKEN_BREAK},
    {"case",     4, TOKEN_CASE},
    {"continue", 8, TOKEN_CONTINUE},
    {"default",  7, TOKEN_DEFAULT},
    {"do",       2, TOKEN_DO},
    {"else",     4, TOKEN_ELSE},
    {"extern",   6, TOKEN_EXTERN},
    {"for",      3, TOKEN_FOR},
    {"goto",     4, TOKEN_GOTO},
    {"if",       2, TOKEN_IF},
    {"int",      3, TOKEN_INT},
    {"long",     4, TOKEN_LONG},
    {"register", 8, TOKEN_REGISTER},
    {"return",   6, TOKEN_RETURN},
    {"static",   6, TOKEN_STATIC},
    {"switch",   6, TOKEN_SWITCH},
    {"void",     4, TOKEN_VOID},
    {"while",    5, TOKEN_WHILE},
};

typedef struct {
    const char *start;
    const char *current;
    const char *line_start;
    size_t line;

    const char *filename;
} lexer;

static lexer lexer_state;

static bool
is_at_end(void)
{
    return *lexer_state.current == '\0';
}

static char
advance(void)
{
    lexer_state.current++;
    return lexer_state.current[-1];
}

static char
peek(void)
{
    return *lexer_state.current;
}

static char
peek_next(void)
{
    return is_at_end() ? '\0' : lexer_state.current[1];
}

static bool
match(const char expected)
{
    if (is_at_end())
        return false;

    if (*lexer_state.current != expected)
        return false;

    lexer_state.current++;
    return true;
}

static bool
is_alpha(const char c)
{
    return ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c == '_'));
}

static bool
is_digit(const char c)
{
    return (c >= '0' && c <= 9);
}

static token
make_token(token_type type)
{
    return (token){
        .type = type,
        .start = lexer_state.start,
        .len = (lexer_state.current - lexer_state.start),
        .filename = lexer_state.filename,
        .line = lexer_state.line,
        .line_start = lexer_state.line_start
    };
}

static bool
skip_block_comments(void)
{
    // '/*' already consumed
    while (!is_at_end()) {
        if (peek() == '*' && peek_next() == '/') {
            advance();
            advance();
            return true;
        } else if (peek() == '\n') {
            lexer_state.line++;
            lexer_state.line_start = lexer_state.current;
            continue;
        }

        advance();
    }

    return false;
}

static bool
skip_whitespace(void)
{
    for (;;) {
        switch(peek()) {
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
            if (peek_next() == '/') {
                advance();
                advance();

                while (!is_at_end() && peek() != '\n')
                    advance();

                break;
            }

            if (peek_next() == '*') {
                advance();
                advance();

                if (!skip_block_comments())
                    return false;
                
                break;
            }
            break;
        default:
            return true;
        }
    }
}

static token_type
identifier_type(void)
{
    size_t keyword_len = lexer_state.current - lexer_state.start;

    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(keyword); i++) {
        const keyword *keyword = &KEYWORDS[i];

        if (keyword_len == keyword->len &&
            memcmp(lexer_state.start, keyword->name, keyword_len) == 0) {
            return keyword->type;
        }
    }

    return TOKEN_IDENTIFIER;
}

static token
identifier(void)
{
    while (is_alpha(peek()) || is_digit(peek()))
        advance();

    return make_token(identifier_type());
}

static token
number(void)
{
    while(is_digit(peek()))
        advance();

    if (peek() == 'l' || peek() == 'L')
        return make_token(TOKEN_LONG_CONSTANT);

    return make_token(TOKEN_INT_CONSTANT);
}

void
lexer_init(const char *source, const char *filename)
{
    lexer_state = (lexer){
        .start = source,
        .current = source,
        .line_start = source,
        .line = 1,
        .filename = filename
    };
}

token
lexer_next_token(void)
{
    if (!skip_whitespace()) {
        lexer_state.start = lexer_state.current;
        return make_token(TOKEN_ERROR);
    }
    lexer_state.start = lexer_state.current;

    if (is_at_end())
        return make_token(TOKEN_EOF);

    const char c = advance();
    if (is_digit(c))
        return number();
    else if (is_alpha(c))
        return identifier();

    switch (c) {
        case '(':
            return make_token(TOKEN_LEFT_PAREN);
        case ')':
            return make_token(TOKEN_RIGHT_PAREN);
        case '{':
            return make_token(TOKEN_LEFT_BRACE);
        case '}':
            return make_token(TOKEN_RIGHT_BRACE);
        case '+':
            if (match('+'))
                return make_token(TOKEN_PLUS_PLUS);
            else if (match('='))
                return make_token(TOKEN_PLUS_EQUAL);
            else
                return make_token(TOKEN_PLUS);
        case '-':
            if (match('-'))
                return make_token(TOKEN_MINUS_MINUS);
            else if (match('='))
                return make_token(TOKEN_MINUS_EQUAL);
            else
                return make_token(TOKEN_MINUS);
        case '*':
            if (match('='))
                return make_token(TOKEN_STAR_EQUAL);
            else 
                return make_token(TOKEN_STAR);
        case '/': 
            if (match('='))
                return make_token(TOKEN_SLASH_EQUAL);
            else
                return make_token(TOKEN_SLASH);
        case '%': 
            if (match('='))
                return make_token(TOKEN_PERCENT_EQUAL);
            else
                return make_token(TOKEN_PERCENT);
        case '~':
            return make_token(TOKEN_TILDE);
        case '=':
            if (match('='))
                return make_token(TOKEN_EQUAL_EQUAL);
            else
                return make_token(TOKEN_EQUAL);
        case '!':
            if (match('='))
                return make_token(TOKEN_BANG_EQUAL);
            else
                return make_token(TOKEN_BANG);
        case '&':
            if (match('&'))
                return make_token(TOKEN_AND_AND);
            else if (match('='))
                return make_token(TOKEN_AND_EQUAL);
            else
                return make_token(TOKEN_AND);
        case '|':
            if (match('|'))
                return make_token(TOKEN_OR_OR);
            else if (match('='))
                return make_token(TOKEN_OR_EQUAL);
            else
                return make_token(TOKEN_OR);
        case '^':
            if (match('='))
                return make_token(TOKEN_CARET_EQUAL);
            else
                return make_token(TOKEN_CARET);
        case '<':
            if (match('='))
                return make_token(TOKEN_LESS_EQUAL);
            else if (match('<')) {
                if (match('='))
                    return make_token(TOKEN_LESS_LESS_EQUAL);
                else
                    return make_token(TOKEN_LESS_LESS);
            }
            else
                return make_token(TOKEN_LESS);
        case '>':
            if (match('='))
                return make_token(TOKEN_GREATER_EQUAL);
            else if (match('>')) {
                if (match('='))
                    return make_token(TOKEN_GREATER_GREATER_EQUAL);
                return
                    make_token(TOKEN_GREATER_GREATER);
            }
            else
                return make_token(TOKEN_GREATER);
        case ';':
            return make_token(TOKEN_SEMICOLON);
        case ':':
            return make_token(TOKEN_COLON);
        case '?':
            return make_token(TOKEN_QUESTION_MARK);
        case ',':
            return make_token(TOKEN_COMMA);
    }

    return make_token(TOKEN_ERROR);
}
