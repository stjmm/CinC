#include <stdio.h>
#include <stdbool.h>

#include "parser.h"
#include "lexer.h"
#include "ast.h"

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

typedef ast_node_t* (*prefix_parse_fn)();
typedef ast_node_t* (*infix_parse_fn)(ast_node_t *left);

typedef struct {
    prefix_parse_fn prefix;
    infix_parse_fn infix;
    precedence_e precedence;
} parse_rule_t;

static parser_t parser;
arena_t *ast_arena;

static ast_node_t *parse_expression(precedence_e precedence);
parse_rule_t *get_rule(token_type_e type);

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

// Prefix parsers
static ast_node_t *number(void)
{
    return ast_new_number(parser.previous);
}

static ast_node_t *unary(void)
{
    token_t op = parser.previous;
    ast_node_t *operand = parse_expression(PREC_UNARY);
    return ast_new_unary(op, operand);
}

// Infix parsers
static ast_node_t *binary(ast_node_t *left)
{
    token_t op = parser.previous;
    parse_rule_t *rule = get_rule(op.type);
    
    // +1 for left-associativity
    ast_node_t *right = parse_expression(rule->precedence + 1);

    return ast_new_binary(op, left, right);
}

static parse_rule_t parse_rules[] = {
    [TOKEN_LEFT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {NULL, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER] = {NULL, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_INT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

parse_rule_t *get_rule(token_type_e type)
{
    return &parse_rules[type];
}

// Here is where the actual "Vaughn-Pratt Parsing" happens
static ast_node_t *parse_expression(precedence_e precedence)
{
    advance();
    prefix_parse_fn prefix_rule = get_rule(parser.previous.type)->prefix;
    if (prefix_rule == NULL) {
        // Handle error
        printf("ERROR!");
    }
    ast_node_t *left = prefix_rule();

    while (precedence <= get_rule(parser.current.type)->precedence) {
        advance();
        infix_parse_fn infix_rule = get_rule(parser.previous.type)->infix;
        left = infix_rule(left);
    }
    
    return left;
}

static ast_node_t *parse_expression_statement(void)
{
    ast_node_t *expr = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_SEMICOLON, "Expected ';' after expression.");
    return expr;
}

ast_node_t *parse_program(const char *source, arena_t *arena)
{
    parser.had_error = false;
    parser.panic_mode = false;
    ast_arena = arena;

    lexer_init(source);
    advance();

    ast_node_t *result = parse_expression_statement();
    return result;
}
