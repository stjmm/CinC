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
    X(TOKEN_SEMICOLON) \
    /* One or two char tokens */ \
    X(TOKEN_MINUS) \
    X(TOKEN_PLUS) \
    X(TOKEN_STAR) \
    X(TOKEN_SLASH) \
    X(TOKEN_EQUAL) \
    /* Literals */ \
    X(TOKEN_IDENTIFIER) \
    X(TOKEN_NUMBER) \
    /* Keywords */ \
    X(TOKEN_INT) \
    X(TOKEN_RETURN) \
    /* Misc */ \
    X(TOKEN_ERROR) \
    X(TOKEN_EOF) \

typedef enum {
#define X(name) name,
    TOKEN_LIST
#undef X
} token_type_e;

typedef struct {
    token_type_e type;
    const char *start;
    unsigned int length;
    unsigned int line;
    unsigned int column;
} token_t;

void lexer_init(const char *source);
token_t lexer_next_token(void);

#endif
