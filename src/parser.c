#include <stdbool.h>
#include <stdlib.h>

#include "parser.h"
#include "arena.h"
#include "lexer.h"
#include "ast.h"

typedef struct {
    token_t previous;
    token_t current;
} parser_t;

typedef ast_node_t* (*prefix_parse_fn)(void);
typedef ast_node_t* (*infix_parse_fn)(ast_node_t *left);

typedef enum {
    PREC_LOWEST,
    PREC_ASSIGNMENT, // =
    PREC_TERM,       // -+
    PREC_FACTOR,     // */
    PREC_UNARY,      // ++x ! -
    PREC_POSTFIX,    // () [] x++
    PREC_PRIMARY,
} precedence_e;

typedef struct {
    prefix_parse_fn prefix;
    infix_parse_fn  infix;
    int precedence;
} parse_rule_t;

static parser_t parser;
static arena_t *phase;

static ast_node_t *ast_new(ast_kind_e kind, token_t token)
{
    ast_node_t *node = ARENA_ALLOC(phase, ast_node_t);
    *node = (ast_node_t){.kind = kind, .token = token };
    return node;
}

static void advance(void)
{
    parser.previous = parser.current;
    parser.current = lexer_next_token();
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
}

static bool match(token_type_e type)
{
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

/* Pratt-Parser Functions */

static ast_node_t *number(void)
{
    ast_node_t *node = ast_new(AST_CONSTANT, parser.previous);
    node->constant.value = strtol(parser.previous.start, NULL, 10);
    return node;
}

static parse_rule_t parse_rules[] = {
    [TOKEN_LEFT_PAREN]    = {NULL, NULL, PREC_LOWEST},
    [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_LOWEST},
    [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_LOWEST},
    [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_LOWEST},
    [TOKEN_LEFT_BRACKET]  = {NULL, NULL, PREC_LOWEST},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_LOWEST},
    [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_LOWEST},
    [TOKEN_MINUS]         = {NULL, NULL, PREC_LOWEST},
    [TOKEN_MINUS_MINUS]   = {NULL, NULL, PREC_LOWEST},
    [TOKEN_PLUS]          = {NULL, NULL, PREC_LOWEST},
    [TOKEN_STAR]          = {NULL, NULL, PREC_LOWEST},
    [TOKEN_SLASH]         = {NULL, NULL, PREC_LOWEST},
    [TOKEN_TILDE]         = {NULL, NULL, PREC_LOWEST},
    [TOKEN_EQUAL]         = {NULL, NULL, PREC_LOWEST},
    [TOKEN_IDENTIFIER]    = {NULL, NULL, PREC_LOWEST},
    [TOKEN_NUMBER]        = {number, NULL, PREC_LOWEST},
    [TOKEN_INT]           = {NULL, NULL, PREC_LOWEST},
    [TOKEN_RETURN]        = {NULL, NULL, PREC_LOWEST},
    [TOKEN_ERROR]         = {NULL, NULL, PREC_LOWEST},
    [TOKEN_EOF]           = {NULL, NULL, PREC_LOWEST},
};

static parse_rule_t *get_rule(token_t tok)
{
    return &parse_rules[tok.type];
}

/* Statements & Declarations */

static ast_node_t *parse_expr_stmt(void)
{
    ast_node_t *expr = NULL;
    return expr;
}

static ast_node_t *parse_stmt(void)
{
    if (match(TOKEN_RETURN)) {
        
    }
}

static ast_node_t *parse_block(void)
{
    ast_node_t *block = ast_new(AST_BLOCK, parser.previous);
}

static ast_node_t *parse_function(void)
{
    consume(TOKEN_IDENTIFIER, "Expected function name.");
    token_t name = parser.previous;

    consume(TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after function name.");

    consume (TOKEN_LEFT_BRACE, "Expected '{' before function body.");
    ast_node_t *body = parse_block();
}

static ast_node_t *parse_declaration(void)
{
    if (match(TOKEN_INT)) {
        return parse_function();
    }

    return NULL;
}

ast_node_t *parse_program(const char *source, arena_t *phase_)
{
    lexer_init(source);
    advance(); // Prime out parser
    phase = phase_;

    ast_node_t *program = AST_NEW(AST_PROGRAM);
    ast_node_t *tail = NULL;
    
    while (!check(TOKEN_EOF)) {
        ast_node_t *decl = parse_declaration();

        if (!tail) program->program.first = decl;
        else tail->next = decl;

        tail = decl;
    }

    return program;
}
