#include "lexer.h"

#include <string.h>

typedef struct {
    const char *start;
    const char *current;
    const char *line_start;
    size_t line;
    const char *filename;
} lexer_t;

typedef struct {
    const char *name;
    size_t len;
    token_kind type;
} keyword_t;

static const keyword_t KEYWORDS[] = {
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

static lexer_t lexer;

static bool
is_at_end(void)
{
    return *lexer.current == '\0';
}

static char
advance(void)
{
    lexer.current++;
    return lexer.current[-1];
}

static char
peek(void)
{
    return *lexer.current;
}

static char
peek_next(void)
{
    return is_at_end() ? '\0' : lexer.current[1];
}

static bool
match(const char expected)
{
    if (is_at_end())
        return false;

    if (*lexer.current != expected)
        return false;

    lexer.current++;
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
    return (c >= '0' && c <= '9');
}

static token_t
make_token(token_kind kind)
{
    return (token_t){
        .kind = kind,
        .start = lexer.start,
        .len = (lexer.current - lexer.start),
        .filename = lexer.filename,
        .line = lexer.line,
        .line_start = lexer.line_start
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
            advance();
            lexer.line++;
            lexer.line_start = lexer.current;
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
            lexer.line++;
            lexer.line_start = lexer.current;
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
            return true;
        default:
            return true;
        }
    }
}

static token_kind
identifier_type(void)
{
    size_t keyword_len = lexer.current - lexer.start;

    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(keyword_t); i++) {
        const keyword_t *keyword = &KEYWORDS[i];

        if (keyword_len == keyword->len &&
            memcmp(lexer.start, keyword->name, keyword_len) == 0) {
            return keyword->type;
        }
    }

    return TOKEN_IDENTIFIER;
}

static token_t
identifier(void)
{
    while (is_alpha(peek()) || is_digit(peek()))
        advance();

    return make_token(identifier_type());
}

static token_t
number(void)
{
    while (is_digit(peek()))
        advance();

    token_kind kind = TOKEN_INT_CONSTANT;

    if (peek() == 'l' || peek() == 'L') {
        advance();
        kind = TOKEN_LONG_CONSTANT;
    }

    // 123abc, 0lL, 1.5 (no floating constants yet)
    if (is_alpha(peek()) || is_digit(peek()) || peek() == '.') {
        while (is_alpha(peek()) || is_digit(peek()) || peek() == '.')
            advance();

        return make_token(TOKEN_ERROR);
    }

    return make_token(kind);
}

void
lexer_init(const char *source, const char *filename)
{
    lexer = (lexer_t){
        .start = source,
        .current = source,
        .line_start = source,
        .line = 1,
        .filename = filename
    };
}

token_t
lexer_peek_token(void)
{
    lexer_t current = lexer;
    token_t peek = lexer_next_token();
    lexer = current;
    return peek;
}

token_t
lexer_next_token(void)
{
    if (!skip_whitespace()) {
        lexer.start = lexer.current;
        return make_token(TOKEN_ERROR);
    }
    lexer.start = lexer.current;

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
        case '[':
            return make_token(TOKEN_LEFT_BRACKET);
        case ']':
            return make_token(TOKEN_RIGHT_BRACKET);
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
