#include <stdlib.h>
#include <stdbool.h>

#include "parser.h"
#include "lexer.h"

typedef struct {
    token_t current;
    token_t previous;
    bool had_error;
    bool panic_mode;
} parser_t;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_PRIMARY,
} precedence_e;

typedef void (*parse_fn)();

typedef struct {
    parse_fn infix;
    parse_fn prefix;
    precedence_e precedence;
} parse_rule_t;


static parser_t parser;


static void advance(void)
{
    parser.previous = parser.current;
    parser.current = lexer_next_token();
}

void consume(token_type_e type, const char *message)
{
    if (parser.current.type == type) {
        advance();
        return;
    }
}

bool match(token_type_e type)
{
    if (parser.current.type == type) {
        advance();
        return true;
    }
    return false;
}

static ast_node_t *statement(void);
static ast_node_t *expression_statement(void);
static ast_node_t *parse_expression(precedence_e precedence);

static ast_node_t *declaration(void)
{
    if (match(TOKEN_INT)) {

    } else {
        return statement();
    }
}

static ast_node_t *statement(void)
{
    if (match(TOKEN_RETURN)) {

    } else {
        return expression_statement();
    }
}

static ast_node_t *expression_statement(void)
{
    ast_node_t *expr = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_SEMICOLON, "Expected ';' after expression.");
}

static parse_rule_t parse_rules[] = {
    [TOKEN_LEFT_PAREN] = {NULL, NULL, NULL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, NULL},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, NULL},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, NULL},
    [TOKEN_LEFT_BRACKET] = {NULL, NULL, NULL},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, NULL},
    [TOKEN_MINUS] = {NULL, NULL, NULL},
    [TOKEN_PLUS] = {NULL, NULL, NULL},
    [TOKEN_STAR] = {NULL, NULL, NULL},
    [TOKEN_SLASH] = {NULL, NULL, NULL},
    [TOKEN_EQUAL] = {NULL, NULL, NULL},
    [TOKEN_IDENTIFIER] = {NULL, NULL, NULL},
    [TOKEN_NUMBER] = {NULL, NULL, NULL},
    [TOKEN_INT] = {NULL, NULL, NULL},
    [TOKEN_RETURN] = {NULL, NULL, NULL},
    [TOKEN_ERROR] = {NULL, NULL, NULL},
    [TOKEN_EOF] = {NULL, NULL, NULL},
};

// Here is where the actual "Vaughn-Pratt Parsing" happens
static ast_node_t *parse_expression(precedence_e precedence)
{
    
}

ast_node_t *parse_program(const char *source)
{
    lexer_init(source);
}
