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

static ast_node_t *parse_statement(void);
static ast_node_t *parse_expression_statement(void);
static ast_node_t *parse_expression(precedence_e precedence);

static ast_node_t *parse_declaration(void)
{
    if (match(TOKEN_INT)) {

    }

    return parse_statement();
}

static ast_node_t *parse_statement(void)
{
    if (match(TOKEN_RETURN)) {

    }

    return parse_expression_statement();
}

static ast_node_t *parse_expression_statement(void)
{
    ast_node_t *expr = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_SEMICOLON, "Expected ';' after expression.");
    return expr;
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

parse_rule_t *get_rule(token_type_e type)
{
    return &parse_rules[type];
}

// Here is where the actual "Vaughn-Pratt Parsing" happens
static ast_node_t *parse_expression(precedence_e precedence)
{
    advance();
    parse_fn prefix_rule = get_rule(parser.current.type)->prefix;
    if (prefix_rule == NULL) {
        // Handle error
    }
    prefix_rule();

    while (precedence <= get_rule(parser.current.type)->precedence) {
        advance();
        parse_fn infix_rule = get_rule(parser.current.type)->infix;
        infix_rule();
    }

}

ast_node_t *parse_program(const char *source)
{
    lexer_init(source);
}
