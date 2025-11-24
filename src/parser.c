#include <stdio.h>
#include <stdbool.h>

#include "lexer.h"
#include "parser.h"

typedef struct {
    token_t previous;
    token_t current;
    bool has_error;
    bool panic_mode;
} parser_t;

parser_t parser;

static void error_at(token_t *token, const char *message)
{
    // Prevents from cascading into fake errors
    if (parser.panic_mode) return;

    parser.panic_mode = true;
    parser.has_error = true;

    fprintf(stderr, "[Line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type != TOKEN_ERROR) {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
}

static void error_at_current(const char *message)
{
    error_at(&parser.current, message);
}

static void error(const char *message)
{
    error_at(&parser.previous, message);
}

static void advance(void)
{
    parser.previous = parser.current;

    for (;;) {
        parser.current = parser.previous;

        if (parser.previous.type != TOKEN_ERROR) break;
        
        error_at_current(parser.current.start);
    }
}

static void consume(token_type_e type, const char *message)
{
    if (parser.current.type == type) {
        advance();
        return;
    }

    error_at_current(message);
}

static void synchronize_error(void)
{
    parser.panic_mode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        switch (parser.current.type) {
            case TOKEN_INT:
            case TOKEN_VOID:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_RETURN:
                return;
            default:
                break;
        }

        advance();
    }
}

ast_node_t *parse(const char *source)
{
    lexer_init(source);
}
