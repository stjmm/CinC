/*
 * On demand lexer for C subset.
 * lexer_token_next() returns one at a time token.
 */

#ifndef CINC_LEXER_H
#define CINC_LEXER_H

#include <stdint.h>
#include <stddef.h>

// X-Macro for all token types
#define TOKEN_LIST \
    /* Single character tokens */ \
    X(TOKEN_LEFT_PAREN)           \
    X(TOKEN_RIGHT_PAREN)          \
    X(TOKEN_LEFT_BRACE)           \
    X(TOKEN_RIGHT_BRACE)          \
    X(TOKEN_LEFT_BRACKET)         \
    X(TOKEN_RIGHT_BRACKET)        \
    X(TOKEN_SEMICOLON)            \
    X(TOKEN_COLON)                \
    X(TOKEN_COMMA)                \
    X(TOKEN_QUESTION_MARK)        \
    /* One or two char tokens */  \
    X(TOKEN_PLUS)                 \
    X(TOKEN_MINUS)                \
    X(TOKEN_STAR)                 \
    X(TOKEN_SLASH)                \
    X(TOKEN_TILDE)                \
    X(TOKEN_CARET)                \
    X(TOKEN_PERCENT)              \
    X(TOKEN_EQUAL)                \
    X(TOKEN_EQUAL_EQUAL)          \
    X(TOKEN_MINUS_MINUS)          \
    X(TOKEN_PLUS_PLUS)            \
    X(TOKEN_BANG)                 \
    X(TOKEN_BANG_EQUAL)           \
    X(TOKEN_PLUS_EQUAL)           \
    X(TOKEN_MINUS_EQUAL)          \
    X(TOKEN_STAR_EQUAL)           \
    X(TOKEN_SLASH_EQUAL)          \
    X(TOKEN_PERCENT_EQUAL)        \
    X(TOKEN_CARET_EQUAL)          \
    X(TOKEN_AND_EQUAL)            \
    X(TOKEN_OR_EQUAL)             \
    X(TOKEN_LESS_LESS_EQUAL)      \
    X(TOKEN_GREATER_GREATER_EQUAL)\
    X(TOKEN_AND)                  \
    X(TOKEN_AND_AND)              \
    X(TOKEN_OR)                   \
    X(TOKEN_OR_OR)                \
    X(TOKEN_LESS)                 \
    X(TOKEN_LESS_EQUAL)           \
    X(TOKEN_LESS_LESS)            \
    X(TOKEN_GREATER)              \
    X(TOKEN_GREATER_EQUAL)        \
    X(TOKEN_GREATER_GREATER)      \
    /* Literals */                \
    X(TOKEN_IDENTIFIER)           \
    X(TOKEN_NUMBER)               \
    /* Keywords */                \
    X(TOKEN_INT)                  \
    X(TOKEN_VOID)                 \
    X(TOKEN_RETURN)               \
    X(TOKEN_IF)                   \
    X(TOKEN_ELSE)                 \
    X(TOKEN_GOTO)                 \
    X(TOKEN_WHILE)                \
    X(TOKEN_DO)                   \
    X(TOKEN_FOR)                  \
    X(TOKEN_BREAK)                \
    X(TOKEN_CONTINUE)             \
    X(TOKEN_SWITCH)               \
    X(TOKEN_CASE)                 \
    X(TOKEN_DEFAULT)              \
    /* Misc */                    \
    X(TOKEN_ERROR)                \
    X(TOKEN_EOF)                  

enum token_type {
#define X(tok_name) tok_name,
    TOKEN_LIST
#undef X
};

struct token {
    enum token_type type;
    const char *start; // Pointer to original source string
    size_t length;

    int line;
    const char *line_start; // Line start for current token 
    const char *resolved; // Set by sema: unique name for this identifier (e.g. "x.0")
    size_t resolved_length;
};

void lexer_init(const char *source);
struct token lexer_next_token(void);

#endif
