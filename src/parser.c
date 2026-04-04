#include <stdbool.h>
#include <stdlib.h>

#include "parser.h"
#include "ast.h"
#include "lexer.h"

struct parser {
    struct token current;
    struct token previous;
    bool had_error;
    bool panic_mode;
};

typedef struct ast_node* (*prefix_parse_fn)(void);
typedef struct ast_node* (*infix_parse_fn)(struct ast_node *);

enum precedence {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_TERM,       // -+
    PREC_FACTOR,     // */
    PREC_UNARY,      // ++x ! -
    PREC_POSTFIX,    // () [] x++
    PREC_PRIMARY,
};

struct parse_rule {
    prefix_parse_fn prefix;
    infix_parse_fn  infix;
    enum precedence prec;
};

static struct parser parser_state;

static void error(struct token *tok, const char *message)
{
    if (parser_state.panic_mode) return;
    parser_state.panic_mode = true;

    parser_state.had_error = true;
}

static void advance(void)
{
    parser_state.previous = parser_state.current;
    for (;;) {
        parser_state.current = lexer_next_token();
        if (parser_state.current.type != TOKEN_ERROR) break;

        error(&parser_state.current, "Unexpected character");
    }
}

static void synchronize(void)
{
    parser_state.panic_mode = false;

    while (parser_state.current.type != TOKEN_EOF) {
        if (parser_state.previous.type == TOKEN_SEMICOLON) return;
        switch (parser_state.current.type) {
            case TOKEN_INT:
            case TOKEN_RETURN:
                return;
            default:
                break;
        }

        advance();
    }
}

static bool check(enum token_type type)
{
    return parser_state.current.type == type;
}

static void consume(enum token_type type, const char *message)
{
    if (check(type)) {
        advance();
        return;
    }

    error(&parser_state.current, message);
}

static bool match(enum token_type type)
{
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

/* Expression parsing (Pratt) */

static struct ast_node *parse_expression(enum precedence prec);
static struct parse_rule *get_rule(enum token_type type);

static struct ast_node *number(void)
{
    long value = strtol(parser_state.previous.start, NULL, 10);
    return AST_NEW(
        AST_CONSTANT,
        parser_state.previous,
        .constant.value = value
    );
}

static struct ast_node *unary(void)
{
    struct token op = parser_state.previous;
    struct ast_node *expr = parse_expression(PREC_UNARY);
    if (!expr) return NULL;
    return AST_NEW(AST_UNARY, op, .unary.expr = expr);
}

static struct ast_node *grouping(void)
{
    struct ast_node *expr = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression");
    return expr;
}

static struct ast_node *binary(struct ast_node *left)
{
    struct token op = parser_state.previous;
    struct parse_rule *rule = get_rule(op.type);
    struct ast_node *right = parse_expression(rule->prec + 1);
    if (!right) return NULL;
    return AST_NEW(AST_BINARY, op, .binary.left = left, .binary.right = right);
}

static struct parse_rule parse_rules[] = {
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

static struct parse_rule *get_rule(enum token_type type)
{
    return &parse_rules[type];
}

static struct ast_node *parse_expression(enum precedence prec)
{
    advance();
    prefix_parse_fn prefix = get_rule(parser_state.previous.type)->prefix;
    if (!prefix) {
        error(&parser_state.previous, "Expected expression");
        return NULL;
    }
    struct ast_node *left = prefix();

    while (prec <= get_rule(parser_state.current.type)->prec) {
        advance();
        infix_parse_fn infix = get_rule(parser_state.previous.type)->infix;
        left = infix(left);
    }

    return left;
}

static struct ast_node *parse_expr_stmt(void)
{
    struct ast_node *expr = parse_expression(PREC_ASSIGNMENT);
    if (!expr) return NULL;
    consume(TOKEN_SEMICOLON, "Expected ';' after expression statement");
    return AST_NEW(AST_EXPR_STMT, parser_state.previous, .expr_stmt.expr = expr);
}

static struct ast_node *parse_statement(void)
{
    if (match(TOKEN_RETURN)) {
        struct token return_tok = parser_state.previous;
        struct ast_node *expr = NULL;
        if (!match(TOKEN_SEMICOLON)) {
            expr = parse_expression(PREC_ASSIGNMENT);
            if (!expr) return NULL;

            consume(TOKEN_SEMICOLON, "Expected ';' after return value");
        }

        return AST_NEW(AST_RETURN, return_tok, .return_stmt.expr = expr);
    }

    return parse_expr_stmt();
}

static struct ast_node *parse_block(void)
{
    struct ast_node *block = AST_NEW(AST_BLOCK, parser_state.previous);
    struct ast_node *tail = NULL;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        struct ast_node *stmt = parse_statement();
        if (!stmt) {
            if (parser_state.panic_mode) { synchronize(); continue; }
        }
        
        if (!tail) block->block.first = stmt;
        else tail->next = stmt;
        tail = stmt;
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after body");
    return block;
}

static struct ast_node *parse_function(void)
{
    struct token return_type = parser_state.previous;
    consume(TOKEN_IDENTIFIER, "Expected function name.");
    struct token name = parser_state.previous;

    consume(TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    if (match(TOKEN_IDENTIFIER)) {}
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after function name.");

    consume(TOKEN_LEFT_BRACE, "Expected '{' before function body.");
    struct ast_node *body = parse_block();

    return AST_NEW(AST_FUNCTION, name,
        .function.name = name,
        .function.return_type = return_type,
        .function.body = body);
}

static struct ast_node *parse_declaration(void)
{
    if (match(TOKEN_INT)) {
        return parse_function();
    }

    error(&parser_state.previous, "Expected declaration");
    return NULL;
}

struct ast_node *parse_program(const char *source)
{
    lexer_init(source);
    advance();

    struct ast_node *program = AST_NEW(AST_PROGRAM, parser_state.previous);
    struct ast_node *tail = NULL;

    while (!check(TOKEN_EOF)) {
        struct ast_node *decl = parse_declaration();
        if (!decl) {
            if (parser_state.panic_mode) { synchronize(); continue; }
        }

        if (!tail) program->program.first = decl;
        else tail->next = decl;
        tail = decl;
    }

    return parser_state.had_error ? NULL : program;
}
