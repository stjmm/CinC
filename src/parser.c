#include <stdbool.h>
#include <stdlib.h>

#include "parser.h"
#include "ast.h"
#include "lexer.h"

typedef struct {
    token_t current;
    token_t previous;
    bool had_error;
    bool panic_mode;
} parser_t;

typedef ast_node_t* (*prefix_parse_fn)(void);
typedef ast_node_t* (*infix_parse_fn)(ast_node_t *);

typedef enum {
    PREC_NONE,
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
    precedence_e precedence;
} parse_rule_t;

static parser_t parser;

static void error(token_t *tok, const char *message)
{
    if (parser.panic_mode) return;
    parser.panic_mode = true;

    parser.had_error = true;
}

static void advance(void)
{
    parser.previous = parser.current;
    for (;;) {
        parser.current = lexer_next_token();
        if (parser.current.type != TOKEN_ERROR) break;

        error(&parser.current, "Unexpected character");
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
                break;
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

    error(&parser.current, message);
}

static bool match(token_type_e type)
{
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

/* Expression parsing (Pratt) */

static ast_node_t *parse_expression(precedence_e prec);
static parse_rule_t *get_rule(token_type_e type);

static ast_node_t *number(void)
{
    long value = strtol(parser.previous.start, NULL, 10);
    return AST_NEW(
        AST_CONSTANT,
        parser.previous,
        .constant.value = value
    );
}

static ast_node_t *unary(void)
{
    token_t op = parser.previous;
    ast_node_t *expr = parse_expression(PREC_UNARY);
    if (!expr) return NULL;
    return AST_NEW(AST_UNARY, op, .unary.expr = expr);
}

static ast_node_t *grouping(void)
{
    ast_node_t *expr = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression");
    return expr;
}

static ast_node_t *binary(ast_node_t *left)
{
    token_t op = parser.previous;
    parse_rule_t *rule = get_rule(op.type);
    ast_node_t *right = parse_expression(rule->precedence + 1);
    if (!right) return NULL;
    return AST_NEW(AST_BINARY, op, .binary.left = left, .binary.right = right);
}

static parse_rule_t parse_rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
    [TOKEN_MINUS_MINUS]   = {NULL, NULL, PREC_UNARY},
    [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
    [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL, binary, PREC_TERM},
    [TOKEN_TILDE]         = {unary, NULL, PREC_UNARY},
    [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER]    = {NULL, NULL, PREC_NONE},
    [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
    [TOKEN_INT]           = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
};

static parse_rule_t *get_rule(token_type_e type)
{
    return &parse_rules[type];
}

static ast_node_t *parse_expression(precedence_e prec)
{
    advance();
    prefix_parse_fn prefix = get_rule(parser.previous.type)->prefix;
    if (!prefix) {
        error(&parser.previous, "Expected expression");
        return NULL;
    }
    ast_node_t *left = prefix();

    while (prec <= get_rule(parser.current.type)->precedence) {
        advance();
        infix_parse_fn infix = get_rule(parser.previous.type)->infix;
        left = infix(left);
    }

    return left;
}

static ast_node_t *parse_expr_stmt(void)
{
    ast_node_t *expr = parse_expression(PREC_ASSIGNMENT);
    if (!expr) return NULL;
    consume(TOKEN_SEMICOLON, "Expected ';' after expression statement");
    return AST_NEW(AST_EXPR_STMT, parser.previous, .expr_stmt.expr = expr);
}

static ast_node_t *parse_statement(void)
{
    if (match(TOKEN_RETURN)) {
        token_t return_tok = parser.previous;
        ast_node_t *expr = NULL;
        if (!match(TOKEN_SEMICOLON)) {
            expr = parse_expression(PREC_ASSIGNMENT);
            if (!expr) return NULL;

            consume(TOKEN_SEMICOLON, "Expected ';' after return value");
        }

        return AST_NEW(AST_RETURN, return_tok, .return_stmt.expr = expr);
    }

    return parse_expr_stmt();
}

static ast_node_t *parse_block(void)
{
    ast_node_t *block = AST_NEW(AST_BLOCK, parser.previous);
    ast_node_t *tail = NULL;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        ast_node_t *stmt = parse_statement();
        if (!stmt) {
            if (parser.panic_mode) { synchronize(); continue; }
        }
        
        if (!tail) block->block.first = stmt;
        else tail->next = stmt;
        tail = stmt;
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after body");
    return block;
}

static ast_node_t *parse_function(void)
{
    token_t return_type = parser.previous;
    consume(TOKEN_IDENTIFIER, "Expected function name.");
    token_t name = parser.previous;

    consume(TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    if (match(TOKEN_IDENTIFIER)) {}
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after function name.");

    consume(TOKEN_LEFT_BRACE, "Expected '{' before function body.");
    ast_node_t *body = parse_block();

    return AST_NEW(AST_FUNCTION, name,
        .function.name = name,
        .function.return_type = return_type,
        .function.body = body);
}

static ast_node_t *parse_declaration(void)
{
    if (match(TOKEN_INT)) {
        return parse_function();
    }

    error(&parser.previous, "Expected declaration");
    return NULL;
}

ast_node_t *parse_program(const char *source)
{
    lexer_init(source);
    advance();

    ast_node_t *program = AST_NEW(AST_PROGRAM, parser.previous);
    ast_node_t *tail = NULL;

    while (!check(TOKEN_EOF)) {
        ast_node_t *decl = parse_declaration();
        if (!decl) {
            if (parser.panic_mode) { synchronize(); continue; }
        }

        if (!tail) program->program.first = decl;
        else tail->next = decl;
        tail = decl;
    }

    return parser.had_error ? NULL : program;
}
