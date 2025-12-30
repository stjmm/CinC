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

static void error_at(token_t *token, const char *message)
{
    // TODO: Add context aware error reporting
    if (parser.panic_mode) return;
    parser.panic_mode = true;

    fprintf(stderr, "[line %d:%d] Error", token->line, token->column);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end of file");
    } else if (token->type == TOKEN_ERROR) {
        // Lexer error
    } else {
        fprintf(stderr, " at '%.*s'\n", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);

    parser.had_error = true;
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
        parser.current = lexer_next_token();
        if (parser.current.type != TOKEN_ERROR) break;

        error_at_current(parser.current.start);
    }
}

static void synchronize(void)
{
    parser.panic_mode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        switch (parser.current.type) {
            case TOKEN_INT:
            case TOKEN_RETURN:
                return;
            default:
                ;
        }

        advance();
    }
}

static bool check(token_type_e type)
{
    return parser.current.type == type;
}

static void consume(token_type_e type, const char *message)
{
    if (check(type)) {
        advance();
        return;
    }

    error_at_current(message);
}

static bool match(token_type_e type)
{
    if (check(type)) {
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

static ast_node_t *grouping(void)
{
    ast_node_t *grouping = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    return grouping;
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
    [TOKEN_LEFT_PAREN]    = {grouping, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS]         = {NULL, binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
    [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER]    = {NULL, NULL, PREC_NONE},
    [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
    [TOKEN_INT]           = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
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
        error("Expected expression.");
        return NULL;
    }
    ast_node_t *left = prefix_rule();

    while (precedence <= get_rule(parser.current.type)->precedence) {
        advance();
        infix_parse_fn infix_rule = get_rule(parser.previous.type)->infix;
        left = infix_rule(left);
        if (!left) return NULL;
    }
    
    return left;
}

//  -- Statements --

static ast_node_t *parse_expression_statement(void)
{
    ast_node_t *expr = parse_expression(PREC_ASSIGNMENT);
    if (!expr) return NULL;

    consume(TOKEN_SEMICOLON, "Expected ';' after expression.");
    return ast_new_expr_stmt(expr);
}

static ast_node_t *parse_statement(void)
{
    if (match(TOKEN_RETURN)) {
        token_t keyword = parser.previous;
        ast_node_t *expr = NULL;

        if (!match(TOKEN_SEMICOLON)) {
            expr = parse_expression(PREC_ASSIGNMENT);
            if (!expr) return NULL;

            consume(TOKEN_SEMICOLON, "Expected ';' after return value.");
        }

        return ast_new_return(keyword, expr);
    }

    return parse_expression_statement();
}

static ast_node_t *parse_block(void)
{
    ast_node_t *block = ast_new_block(parser.previous);

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        ast_node_t *stmt = parse_statement();
        if (stmt)
            ast_block_append(block, stmt);
        else if (parser.panic_mode)
            synchronize();
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after body.");

    return block;
}

// -- Declarations --

static ast_node_t* parse_function(token_t return_type)
{
    consume(TOKEN_IDENTIFIER, "Expected function name.");
    token_t name = parser.previous;

    consume(TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    // TODO: Parameters
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after function name.");

    consume(TOKEN_LEFT_BRACE, "Expected '{' before function body.");
    ast_node_t *body = parse_block();
    if (!body) return NULL;

    return ast_new_function(name, return_type, body);
}

static ast_node_t *parse_declaration(void)
{
    if (match(TOKEN_INT)) {
        token_t return_type = parser.previous;
        return parse_function(return_type);
    }

    error_at_current("Expected declaration.");
    return NULL;
}

ast_node_t *parse_program(const char *source, arena_t *arena)
{
    parser.had_error = false;
    parser.panic_mode = false;
    ast_arena = arena;

    lexer_init(source);
    advance();

    ast_node_t *program = ast_new_program();
    while (parser.current.type != TOKEN_EOF) {
        ast_node_t *decl = parse_declaration();
        if (decl)
            ast_program_append(program, decl);

        if (parser.panic_mode)
            synchronize();
    }

    return parser.had_error ? NULL : program;
}
