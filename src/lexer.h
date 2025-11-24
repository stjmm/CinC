#ifndef CINC_LEXER_H
#define CINC_LEXER_H

#define TOKEN_LIST \
    /* Single character tokens */ \
    X(TOKEN_LEFT_PAREN) \
    X(TOKEN_RIGHT_PAREN) \
    X(TOKEN_LEFT_BRACE) \
    X(TOKEN_RIGHT_BRACE) \
    X(TOKEN_LEFT_BRACKET) \
    X(TOKEN_RIGHT_BRACKET) \
    X(TOKEN_COMMA) \
    X(TOKEN_DOT) \
    X(TOKEN_SEMICOLON) \
    X(TOKEN_QUESTION) \
    \
    /* One or two-character tokens */ \
    X(TOKEN_MINUS) \
    X(TOKEN_MINUS_MINUS) \
    X(TOKEN_MINUS_EQUAL) \
    X(TOKEN_ARROW) \
    X(TOKEN_PLUS) \
    X(TOKEN_PLUS_PLUS) \
    X(TOKEN_PLUS_EQUAL) \
    X(TOKEN_STAR) \
    X(TOKEN_STAR_EQUAL) \
    X(TOKEN_SLASH) \
    X(TOKEN_SLASH_EQUAL) \
    X(TOKEN_PERCENT) \
    X(TOKEN_PERCENT_EQUAL) \
    \
    X(TOKEN_AMPERSAND) \
    X(TOKEN_AMPERSAND_AMPERSAND) \
    X(TOKEN_AMPERSAND_EQUAL) \
    X(TOKEN_PIPE) \
    X(TOKEN_PIPE_PIPE) \
    X(TOKEN_PIPE_EQUAL) \
    X(TOKEN_CARET) \
    X(TOKEN_CARET_EQUAL) \
    X(TOKEN_EQUAL) \
    X(TOKEN_EQUAL_EQUAL) \
    X(TOKEN_BANG) \
    X(TOKEN_BANG_EQUAL) \
    \
    X(TOKEN_GREATER) \
    X(TOKEN_GREATER_EQUAL) \
    X(TOKEN_GREATER_GREATER) \
    X(TOKEN_LESS) \
    X(TOKEN_LESS_EQUAL) \
    X(TOKEN_LESS_LESS) \
    X(TOKEN_GREATER_GREATER_EQUAL) \
    X(TOKEN_LESS_LESS_EQUAL) \
    \
    /* Literals */ \
    X(TOKEN_IDENTIFIER) \
    X(TOKEN_CONSTANT) \
    X(TOKEN_STRING_LITERAL) \
    X(TOKEN_CHAR_LITERAL) \
    \
    /* Keywords (Storage and types) */ \
    X(TOKEN_AUTO) \
    X(TOKEN_REGISTER) \
    X(TOKEN_STATIC) \
    X(TOKEN_EXTERN) \
    X(TOKEN_TYPEDEF) \
    X(TOKEN_STRUCT) \
    X(TOKEN_VOID) \
    X(TOKEN_CHAR) \
    X(TOKEN_SHORT) \
    X(TOKEN_INT) \
    X(TOKEN_LONG) \
    X(TOKEN_FLOAT) \
    X(TOKEN_DOUBLE) \
    X(TOKEN_SIGNED) \
    X(TOKEN_UNSIGNED) \
    X(TOKEN_CONST) \
    X(TOKEN_UNION) \
    \
    /* Keywords (Control flow) */ \
    X(TOKEN_IF) \
    X(TOKEN_ELSE) \
    X(TOKEN_SWITCH) \
    X(TOKEN_CASE) \
    X(TOKEN_DEFAULT) \
    X(TOKEN_WHILE) \
    X(TOKEN_DO) \
    X(TOKEN_FOR) \
    X(TOKEN_GOTO) \
    X(TOKEN_CONTINUE) \
    X(TOKEN_BREAK) \
    X(TOKEN_RETURN) \
    X(TOKEN_SIZEOF) \
    \
    /* Misc */ \
    X(TOKEN_EOF) \
    X(TOKEN_ERROR)

// Check X macros to understand how it unrolls the token list
typedef enum {
    #define X(name) name,
    TOKEN_LIST
    #undef X
} token_type_e;

typedef struct {
    token_type_e type;
    const char *start;
    int line;
    int length;
} token_t;

void lexer_init(const char *source);
token_t lexer_scan_token(void);

#endif
