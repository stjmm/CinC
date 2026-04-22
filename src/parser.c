#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "parser.h"
#include "ast.h"
#include "lexer.h"

struct parser {
    struct token previous;
    struct token current;
    bool had_error;
    bool panic_mode;
};

typedef struct ast_node* (*prefix_parse_fn)(void);
typedef struct ast_node* (*infix_parse_fn)(struct ast_node *);

// C precedences
enum precedence {
    PREC_NONE,
    PREC_ASSIGNMENT,    // = +=
    PREC_TERNARY,       // ?:
    PREC_OR,            // || 
    PREC_AND,           // &&
    PREC_BITWISE_OR,    // |
    PREC_BITWISE_XOR,   // ^
    PREC_BITWISE_AND,   // &
    PREC_EQUALITY,      // == !=
    PREC_COMPARISON,    // < > <= >=
    PREC_BITWISE_SHIFT, // << >>
    PREC_TERM,          // -+
    PREC_FACTOR,        // */%
    PREC_UNARY,         // ++x ! -
    PREC_POSTFIX,       // () [] x++
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

    int col = (int)(tok->start - tok->line_start);

    fprintf(stderr, "Error at line %d, col %d: %s\n", tok->line, col, message);

    const char *line_end = tok->line_start;
    while (*line_end != '\0' && *line_end != '\n')
        line_end++;
    fprintf(stderr, "  %.*s\n", (int)(line_end - tok->line_start), tok->line_start);

    fprintf(stderr, "  %*s", col, "");
    for (size_t i = 0; i < (tok->length > 0 ? tok->length : 1); i++)
        fputc('^', stderr);
    fputc('\n', stderr);

    parser_state.had_error = true;
}

static void advance(void)
{
    parser_state.previous = parser_state.current;
    for (;;) {
        parser_state.current = lexer_next_token();
        if (parser_state.current.type != TOKEN_ERROR)
            break;

        error(&parser_state.current, "Unexpected character");
    }
}

static void synchronize_block_item(void)
{
    parser_state.panic_mode = false;

    while (parser_state.current.type != TOKEN_EOF) {
        if (parser_state.previous.type == TOKEN_SEMICOLON)
            return;

        switch (parser_state.current.type) {
            case TOKEN_INT:
            case TOKEN_RETURN:
            case TOKEN_IF:
            case TOKEN_ELSE:
            case TOKEN_FOR:
            case TOKEN_WHILE:
            case TOKEN_DO:
            case TOKEN_CONTINUE:
            case TOKEN_BREAK:
            case TOKEN_SWITCH:
            case TOKEN_CASE:
            case TOKEN_DEFAULT:
            case TOKEN_GOTO:
                return;
            default:
                break;
        }

        advance();
    }
}

static void synchronize_translation_unit(void)
{
    parser_state.panic_mode = false;

    while (parser_state.current.type != TOKEN_EOF) {
        switch (parser_state.current.type) {
            case TOKEN_INT:
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

    error(&parser_state.previous, message);
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

static struct ast_node *identifier(void)
{
    return AST_NEW(
        AST_IDENTIFIER,
        parser_state.previous
    );
}

static struct ast_node *unary(void)
{
    struct token op = parser_state.previous;
    struct ast_node *expr = parse_expression(PREC_UNARY);

    if (!expr)
        return NULL;
    return AST_NEW(AST_UNARY, op, .unary.expr = expr);
}

static struct ast_node *pre(void)
{
    struct token op = parser_state.previous;
    struct ast_node *expr = parse_expression(PREC_UNARY);
    if (!expr) return NULL;
    return AST_NEW(AST_PRE, op, .unary.expr = expr);
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

    if (!right)
        return NULL;
    return AST_NEW(AST_BINARY, op, .binary.left = left, .binary.right = right);
}

static struct ast_node *assignment(struct ast_node *left)
{
    struct token op = parser_state.previous;

    struct ast_node *right = parse_expression(PREC_ASSIGNMENT);
    if (!right)
        return NULL;

    return AST_NEW(AST_ASSIGNMENT, op,
            .assignment.lvalue = left,
            .assignment.rvalue = right
    );
}

static struct ast_node *post(struct ast_node *left)
{
    struct token op = parser_state.previous;
    return AST_NEW(AST_POST, op, .unary.expr = left);
}

static struct ast_node *ternary(struct ast_node *left)
{
    struct token ternary_tok = parser_state.previous;
    
    struct ast_node *then = parse_expression(PREC_ASSIGNMENT);
    if (!then)
        return NULL;

    consume(TOKEN_COLON, "Expected ':' after conditional expression");
    struct ast_node *else_then = parse_expression(PREC_TERNARY);
    if (!else_then)
        return NULL;

    return AST_NEW(AST_TERNARY, ternary_tok,
            .ternary.condition = left,
            .ternary.then = then,
            .ternary.else_then = else_then,
    );
}

/* Each token maps to a prefix rule at the start of an expression,
 * an infix rule and minimum precedence level for infix use. */
static struct parse_rule parse_rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
    [TOKEN_QUESTION_MARK] = {NULL, ternary, PREC_TERNARY},
    [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
    [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
    [TOKEN_TILDE]         = {unary, NULL, PREC_UNARY},
    [TOKEN_CARET]         = {NULL, binary, PREC_BITWISE_XOR},
    [TOKEN_MINUS_MINUS]   = {pre, post, PREC_POSTFIX},
    [TOKEN_PLUS_PLUS]     = {pre, post, PREC_POSTFIX},
    [TOKEN_EQUAL]         = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_PLUS_EQUAL]    = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_MINUS_EQUAL]   = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_STAR_EQUAL]    = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_SLASH_EQUAL]   = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_PERCENT_EQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_AND_EQUAL]     = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_OR_EQUAL]      = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_CARET_EQUAL]   = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_LESS_LESS_EQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_GREATER_GREATER_EQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [TOKEN_EQUAL_EQUAL]   = {NULL, binary, PREC_EQUALITY},
    [TOKEN_BANG_EQUAL]    = {NULL, binary, PREC_EQUALITY},
    [TOKEN_LESS]          = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_LESS]     = {NULL, binary, PREC_BITWISE_SHIFT},
    [TOKEN_GREATER]       = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_GREATER] = {NULL, binary, PREC_BITWISE_SHIFT},
    [TOKEN_AND_AND]       = {NULL, binary, PREC_AND},
    [TOKEN_OR_OR]         = {NULL, binary, PREC_OR},
    [TOKEN_OR]            = {NULL, binary, PREC_BITWISE_OR},
    [TOKEN_AND]           = {NULL, binary, PREC_BITWISE_AND},
    [TOKEN_IDENTIFIER]    = {identifier, NULL, PREC_NONE},
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

/* Core Pratt dispatch: parse an expression at given precedence level.
 * Calls prefix rule for current token, then keeps consuming
 * infix operators as long as they bind tighter than prec */
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

static struct ast_node *parse_block(void);
static struct ast_node *parse_declaration(void);

static struct ast_node *parse_statement(void)
{
    if (match(TOKEN_LEFT_BRACE)) {
        return parse_block();
    }

    if (match(TOKEN_RETURN)) {
        struct token return_tok = parser_state.previous;
        struct ast_node *expr = NULL;

        if (!match(TOKEN_SEMICOLON) && !check(TOKEN_EOF)) {
            expr = parse_expression(PREC_ASSIGNMENT);
            if (!expr)
                return NULL;
        }

        consume(TOKEN_SEMICOLON, "Expected ';' after return value");
        return AST_NEW(AST_RETURN, return_tok, .return_stmt.expr = expr);
    }
    
    if (match(TOKEN_IF)) {
        struct token if_tok = parser_state.previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'");
        struct ast_node *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'if' condition");

        if (check(TOKEN_ELSE) || check(TOKEN_RIGHT_BRACE) || check(TOKEN_EOF)) {
            error(&parser_state.previous, "Expected statement after 'if'");
            return NULL;
        }
        struct ast_node *then = parse_statement();
        if (!then) return NULL;

        struct ast_node *else_then = NULL;
        if (match(TOKEN_ELSE))
            else_then = parse_statement();

        return AST_NEW(AST_IF_STMT, if_tok,
                .if_stmt.condition = cond,
                .if_stmt.then = then,
                .if_stmt.else_then = else_then
        );
    }

    if (match(TOKEN_CASE)) {
        struct token case_tok = parser_state.previous;
        struct ast_node *value = parse_expression(PREC_ASSIGNMENT);
        if (!value)
            return NULL;
        consume(TOKEN_COLON, "Expected ':' after 'case'");

        struct ast_node *body_head = NULL;
        struct ast_node *body_tail = NULL;
        while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
                !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
            struct ast_node *stmt = parse_statement();
            if (!stmt)
                return NULL;
            LIST_APPEND(body_head, body_tail, stmt);
        }

        return AST_NEW(AST_CASE, case_tok,
                .case_stmt.value = value,
                .case_stmt.first = body_head,
                .case_stmt.label = NULL
        );
    }

    if (match(TOKEN_DEFAULT)) {
        struct token default_tok = parser_state.previous;
        consume(TOKEN_COLON, "Expected ':' after 'default'");

        struct ast_node *body_head = NULL;
        struct ast_node *body_tail = NULL;
        while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
                !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
            struct ast_node *stmt = parse_statement();
            if (!stmt)
                return NULL;
            LIST_APPEND(body_head, body_tail, stmt);
        }

        return AST_NEW(AST_DEFAULT, default_tok,
                .default_stmt.first = body_head,
                .default_stmt.label = NULL
        );
    }

    if (match(TOKEN_SWITCH)) {
        struct token switch_tok = parser_state.previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'switch'");
        struct ast_node *cond = parse_expression(PREC_ASSIGNMENT);
        if (!cond) return NULL;
        consume(TOKEN_RIGHT_PAREN, "Expected '(' after 'switch'");

        struct ast_node *body = parse_statement();
        if (!body)
            return NULL;

        return AST_NEW(AST_SWITCH, switch_tok,
                .switch_stmt.condition = cond,
                .switch_stmt.body = body,
                .switch_stmt.label = NULL,
                .switch_stmt.annotation = NULL
        );
    }

    if (match(TOKEN_FOR)) {
        struct token for_tok = parser_state.previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'for'");

        // Either declaration, expression or empty
        struct ast_node *for_init = NULL;
        if (match(TOKEN_INT)) {
            for_init = parse_declaration();
            if (!for_init)
                return NULL;
        } else if (!match(TOKEN_SEMICOLON)) {
            for_init = parse_expression(PREC_ASSIGNMENT);
            if (!for_init)
                return NULL;
            consume(TOKEN_SEMICOLON, "Expected ';' after for-init");
        }

        // Cond: optional
        struct ast_node *condition = NULL;
        if (!match(TOKEN_SEMICOLON)) {
            condition = parse_expression(PREC_ASSIGNMENT);
            if (!condition)
                return NULL;
            consume(TOKEN_SEMICOLON, "Expected ';' after for-condition");
        }

        struct ast_node *post = NULL;
        if (!check(TOKEN_RIGHT_PAREN)) {
            post = parse_expression(PREC_ASSIGNMENT);
            if (!post)
                return NULL;
        }

        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'for' clauses");

        struct ast_node *body = parse_statement();
        if (!body)
            return NULL;

        return AST_NEW(AST_FOR, for_tok,
                .for_stmt.for_init = for_init,
                .for_stmt.condition = condition,
                .for_stmt.post = post,
                .for_stmt.body = body,
                .for_stmt.label = NULL
        );
    }

    if (match(TOKEN_WHILE)) {
        struct token while_tok = parser_state.previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'");
        struct ast_node *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'while' condition");

        struct ast_node *body = parse_statement();
        if (!body) {
            error(&parser_state.previous, "Empty 'while' loop body");
            return NULL;
        }

        return AST_NEW(AST_WHILE, while_tok,
                .while_stmt.condition = cond,
                .while_stmt.body = body,
                .while_stmt.label = NULL
        );
    }

    if (match(TOKEN_DO)) {
        struct token do_tok = parser_state.previous;

        struct ast_node *body = parse_statement();
        if (!body) {
            error(&parser_state.previous, "Empty 'do-while' loop body");
            return NULL;
        }

        consume(TOKEN_WHILE, "Expected 'while' after 'do-while' body");

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'do-while'");
        struct ast_node *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'do-while' condition");

        consume(TOKEN_SEMICOLON, "Expected ';' after 'do-while'");

        return AST_NEW(AST_DOWHILE, do_tok,
                .do_while.body = body,
                .do_while.condition = cond,
                .do_while.label = NULL
        );
    }

    if (match(TOKEN_GOTO)) {
        struct token goto_tok = parser_state.previous;
        consume(TOKEN_IDENTIFIER, "Expected label after 'goto'");
        struct token label = parser_state.previous;
        consume(TOKEN_SEMICOLON, "Expected ';' after goto statement");

        return AST_NEW(AST_GOTO, goto_tok, .goto_stmt.label = label);
    }

    if (match(TOKEN_BREAK)) {
        consume(TOKEN_SEMICOLON, "Expected ';' after 'break'");
        return AST_NEW(AST_BREAK, parser_state.previous, .break_stmt.target_label = NULL);
    }

    if (match(TOKEN_CONTINUE)) {
        consume(TOKEN_SEMICOLON, "Expected ';' after 'continue'");
        return AST_NEW(AST_CONTINUE, parser_state.previous, .continue_stmt.target_label = NULL);
    }

    if (match(TOKEN_SEMICOLON))
        return AST_NEW(AST_NULL_STMT, parser_state.previous);

    // If didn't match with any statement
    // It's either an expression-statement or goto label
    struct ast_node *expr = parse_expression(PREC_ASSIGNMENT);
    if (!expr)
        return NULL;

    // We use AST_IDENTIFIER for goto labels
    // We parsed an expression and if that expression is
    // AST_IDENTIFIER with ':' it's a goto label
    if (expr->type == AST_IDENTIFIER) {
        if (match(TOKEN_COLON)) {
            struct ast_node *stmt = parse_statement();
            return AST_NEW(AST_LABEL_STMT, expr->token,
                    .label_stmt.stmt = stmt,
                    .label_stmt.name = expr->token
            );
        }
    }

    // Othwerwise expression statement
    consume(TOKEN_SEMICOLON, "Expected ';' after expression statement");
    return AST_NEW(AST_EXPR_STMT, parser_state.previous, .expr_stmt.expr = expr);
}

static struct ast_node *parse_declaration(void)
{
    struct token return_type = parser_state.previous;

    consume(TOKEN_IDENTIFIER, "Expected variable name");
    struct ast_node *name = AST_NEW(AST_IDENTIFIER, parser_state.previous);

    struct ast_node *init = NULL;
    if (match(TOKEN_EQUAL))
        init = parse_expression(PREC_ASSIGNMENT);

    consume(TOKEN_SEMICOLON, "Expected ';' after variable");
    
    return AST_NEW(AST_DECLARATION, name->token, 
            .declaration.name = name,
            .declaration.init = init
    );
}

static struct ast_node *parse_block(void)
{
    struct ast_node *block = AST_NEW(AST_BLOCK, parser_state.previous);
    struct ast_node *tail = NULL;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        struct ast_node *item = NULL;
        if (match(TOKEN_INT))
            item = parse_declaration();
        else
            item = parse_statement();

        if (!item || parser_state.panic_mode) {
            synchronize_block_item();
            continue;
        }
        
        LIST_APPEND(block->block.first, tail, item);
    }

    if (!parser_state.had_error)
        consume(TOKEN_RIGHT_BRACE, "Expected '}' after body");
    else 
        match(TOKEN_RIGHT_BRACE);

    return block;
}

static struct ast_node *parse_function(void)
{
    struct token return_type = parser_state.previous;

    consume(TOKEN_IDENTIFIER, "Expected function name");
    struct token name_tok = parser_state.previous;
    struct ast_node *name = AST_NEW(AST_IDENTIFIER, parser_state.previous);

    consume(TOKEN_LEFT_PAREN, "Expected '(' after function name");
    consume(TOKEN_VOID, "Expected 'void' in parameters list");
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after function name");

    consume(TOKEN_LEFT_BRACE, "Expected '{' before function body");
    struct ast_node *body = parse_block();

    return AST_NEW(AST_FUNCTION, name_tok,
        .function.name = name,
        .function.return_type = return_type,
        .function.body = body);
}

static struct ast_node *parse_external_declaration(void)
{
    if (match(TOKEN_INT))
        return parse_function();

    error(&parser_state.current, "Expected declaration");
    return NULL;
}

struct ast_node *parse_translation_unit(const char *source)
{
    lexer_init(source);
    advance();

    struct ast_node *program = AST_NEW(AST_PROGRAM, parser_state.previous);
    struct ast_node *tail = NULL;

    while (!check(TOKEN_EOF)) {
        struct ast_node *decl = parse_external_declaration();
        if (!decl) {
            synchronize_translation_unit();
            continue;
        }

        LIST_APPEND(program->program.first, tail, decl);
    }

    return parser_state.had_error ? NULL : program;
}
