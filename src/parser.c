#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "parser.h"
#include "ast.h"
#include "lexer.h"
#include "type.h"

struct parser {
    struct token previous;
    struct token current;
    bool had_error;
    bool panic_mode;
};

typedef struct expr *(*prefix_parse_fn)(void);
typedef struct expr *(*infix_parse_fn)(struct expr *);

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
    infix_parse_fn infix;
    enum precedence prec;
};

struct decl_specs {
    struct type *base_type;
    enum storage_class storage_class;
    struct token type_tok;
    struct token storage_tok;
};

static struct parser parser_state;

static void error(struct token *tok, const char *message)
{
    if (parser_state.panic_mode)
        return;

    parser_state.panic_mode = true;

    int col = (int)(tok->start - tok->line_start);

    fprintf(stderr, "Error at line %d, col %d: %s\n", tok->line, col, message);

    const char *line_end = tok->line_start;
    while (*line_end != '\0' && *line_end != '\n')
        line_end++;
    fprintf(stderr, "  %.*s\n", (int)(line_end - tok->line_start), tok->line_start);

    fprintf(stderr, "  %*s", col, "");
    for (int i = 0; i < (tok->length > 0 ? tok->length : 1); i++)
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

static bool check(enum token_type type)
{
    return parser_state.current.type == type;
}

static bool match(enum token_type type)
{
    if (!check(type))
        return false;

    advance();
    return true;
}

static void consume(enum token_type type, const char *message)
{
    if (check(type)) {
        advance();
        return;
    }

    error(&parser_state.current, message);
}

static bool is_type_specifier(enum token_type type)
{
    return type == TOKEN_INT || type == TOKEN_VOID;
}

static bool is_storage_class_specifier(enum token_type type)
{
    return type == TOKEN_STATIC ||
           type == TOKEN_EXTERN ||
           type == TOKEN_AUTO   ||
           type == TOKEN_REGISTER;
}

static bool is_declaration_start(enum token_type type)
{
    return is_type_specifier(type) || is_storage_class_specifier(type);
}

static void synchronize_block_item(void)
{
    parser_state.panic_mode = false;

    while (parser_state.current.type != TOKEN_EOF) {
        if (parser_state.previous.type == TOKEN_SEMICOLON)
            return;

        if (is_declaration_start(parser_state.current.type))
            return;

        switch (parser_state.current.type) {
            case TOKEN_RETURN:
            case TOKEN_IF:
            case TOKEN_ELSE:
            case TOKEN_FOR:
            case TOKEN_WHILE:
            case TOKEN_DO:
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
            case TOKEN_SWITCH:
            case TOKEN_CASE:
            case TOKEN_DEFAULT:
            case TOKEN_GOTO:
            case TOKEN_RIGHT_BRACE:
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
        if (is_declaration_start(parser_state.current.type))
            return;

        advance();
    }
}

/* Expression parsing */

static struct expr *parse_expression(enum precedence prec);
static struct parse_rule *get_rule(enum token_type type);

static struct expr *number(void)
{
    struct expr *expr = expr_new(EXPR_INT_LITERAL, parser_state.previous);
    expr->int_value = strtol(parser_state.previous.start, NULL, 10);
    return expr;
}

static struct expr *identifier(void)
{
    struct expr *expr = expr_new(EXPR_IDENTIFIER, parser_state.previous);
    expr->identifier.name = parser_state.previous;
    return expr;
}

static struct expr *unary(void)
{
    struct token op = parser_state.previous;
    struct expr *operand = parse_expression(PREC_UNARY);
    if (!operand)
        return NULL;

    struct expr *expr = expr_new(EXPR_UNARY, op);
    expr->unary.op = op;
    expr->unary.operand = operand;
    return expr;
}

static struct expr *pre(void)
{
    struct token op = parser_state.previous;
    struct expr *operand = parse_expression(PREC_UNARY);
    if (!operand)
        return NULL;

    struct expr *expr = expr_new(EXPR_PRE, op);
    expr->unary.op = op;
    expr->unary.operand = operand;
    return expr;
}

static struct expr *grouping(void)
{
    struct expr *expr = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression");
    return expr;
}

static struct expr *binary(struct expr *left)
{
    struct token op = parser_state.previous;
    struct parse_rule *rule = get_rule(op.type);

    struct expr *right = parse_expression(rule->prec + 1);
    if (!right)
        return NULL;

    struct expr *expr = expr_new(EXPR_BINARY, op);
    expr->binary.op = op;
    expr->binary.left = left;
    expr->binary.right = right;
    return expr;
}

static struct expr *assignment(struct expr *left)
{
    struct token op = parser_state.previous;
    struct expr *right = parse_expression(PREC_ASSIGNMENT);
    if (!right)
        return NULL;

    struct expr *expr = expr_new(EXPR_ASSIGNMENT, op);
    expr->assignment.op = op;
    expr->assignment.lvalue = left;
    expr->assignment.rvalue = right;
    return expr;
}

static struct expr *post(struct expr *left)
{
    struct token op = parser_state.previous;

    struct expr *expr = expr_new(EXPR_POST, op);
    expr->unary.op = op;
    expr->unary.operand = left;
    return expr;
}

static struct expr *ternary(struct expr *left)
{
    struct token tok = parser_state.previous; // ? tok
    
    struct expr *then_expr = parse_expression(PREC_ASSIGNMENT);
    if (!then_expr)
        return NULL;

    consume(TOKEN_COLON, "Expected ':' after conditional expression");
    struct expr *else_expr = parse_expression(PREC_TERNARY);
    if (!else_expr)
        return NULL;

    struct expr *expr = expr_new(EXPR_CONDITIONAL, tok);
    expr->conditional.condition = left;
    expr->conditional.then_expr = then_expr;
    expr->conditional.else_expr = else_expr;
    return expr;
}

static struct expr *call(struct expr *left)
{
    struct token tok = parser_state.previous;
    struct expr *args_head = NULL;
    struct expr *args_tail = NULL;

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            struct expr *arg = parse_expression(PREC_ASSIGNMENT);
            if (!arg)
                return NULL;
            LIST_APPEND(args_head, args_tail, arg);
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expected ')' after arguments");

    struct expr *expr = expr_new(EXPR_CALL, tok);
    expr->call.callee = left;
    expr->call.args = args_head;
    return expr;
}

/* Each token maps to a prefix rule at the start of an expression,
 * an infix rule and minimum precedence level for infix use. */
static struct parse_rule parse_rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call, PREC_POSTFIX},
    [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON]         = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA]         = {NULL, NULL, PREC_NONE},
    [TOKEN_QUESTION_MARK] = {NULL, ternary, PREC_TERNARY},

    [TOKEN_PLUS]          = {unary, binary, PREC_TERM},
    [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
    [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL, binary, PREC_FACTOR},
    
    [TOKEN_AND_AND]       = {NULL, binary, PREC_AND},
    [TOKEN_OR_OR]         = {NULL, binary, PREC_OR},

    [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
    [TOKEN_TILDE]         = {unary, NULL, PREC_NONE},

    [TOKEN_CARET]         = {NULL, binary, PREC_BITWISE_XOR},
    [TOKEN_OR]            = {NULL, binary, PREC_BITWISE_OR},
    [TOKEN_AND]           = {NULL, binary, PREC_BITWISE_AND},

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

    [TOKEN_IDENTIFIER]    = {identifier, NULL, PREC_NONE},
    [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},

    [TOKEN_INT]           = {NULL, NULL, PREC_NONE},
    [TOKEN_VOID]          = {NULL, NULL, PREC_NONE},
    [TOKEN_STATIC]        = {NULL, NULL, PREC_NONE},
    [TOKEN_EXTERN]        = {NULL, NULL, PREC_NONE},
    [TOKEN_AUTO]          = {NULL, NULL, PREC_NONE},
    [TOKEN_REGISTER]      = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
    [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
    [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE]         = {NULL, NULL, PREC_NONE},
    [TOKEN_DO]            = {NULL, NULL, PREC_NONE},
    [TOKEN_BREAK]         = {NULL, NULL, PREC_NONE},
    [TOKEN_CONTINUE]      = {NULL, NULL, PREC_NONE},
    [TOKEN_SWITCH]        = {NULL, NULL, PREC_NONE},
    [TOKEN_CASE]          = {NULL, NULL, PREC_NONE},
    [TOKEN_DEFAULT]       = {NULL, NULL, PREC_NONE},
    [TOKEN_GOTO]          = {NULL, NULL, PREC_NONE},

    [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
};

static struct parse_rule *get_rule(enum token_type type)
{
    return &parse_rules[type];
}

static struct expr *parse_expression(enum precedence prec)
{
    advance();
    prefix_parse_fn prefix = get_rule(parser_state.previous.type)->prefix;
    if (!prefix) {
        error(&parser_state.previous, "Expected expression");
        return NULL;
    }

    struct expr *left = prefix();

    while (prec <= get_rule(parser_state.current.type)->prec) {
        advance();
        infix_parse_fn infix = get_rule(parser_state.previous.type)->infix;
        left = infix(left);
    }

    return left;
}

static struct stmt *parse_statement(void);
static struct block_item *parse_block_item(void);
static struct stmt *parse_block_after_lbrace(void);
static struct decl *parse_declaration(void);

static struct block_item *parse_case_default_items()
{
    // TODO: Add break stopping?
    struct block_item *head = NULL;
    struct block_item *tail = NULL;

    bool first = true;
    while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
            !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (first && is_declaration_start(parser_state.current.type)) {
            error(&parser_state.current, "Label followed by declaration");
            return NULL;
        }

        struct block_item *item = parse_block_item();
        if (!item)
            return NULL;

        first = false;
        LIST_APPEND(head, tail, item);
    }

    return head;
}

static struct stmt *parse_statement(void)
{
     /*
     * TODO: Should this guard be here or in sema?
     */
    if (is_declaration_start(parser_state.current.type)) {
        error(&parser_state.current, "Expected statement, not declaration");
        return NULL;
    }

    if (match(TOKEN_LEFT_BRACE))
        return parse_block_after_lbrace();

    if (match(TOKEN_RETURN)) {
        struct token tok = parser_state.previous;
        struct expr *expr = NULL;

        if (!check(TOKEN_SEMICOLON) && !check(TOKEN_EOF)) {
            expr = parse_expression(PREC_ASSIGNMENT);
            if (!expr)
                return NULL;
        }

        consume(TOKEN_SEMICOLON, "Expected ';' after return");

        struct stmt *stmt = stmt_new(STMT_RETURN, tok);
        stmt->return_stmt.expr = expr;
        return stmt;
    }

    if (match(TOKEN_IF)) {
        struct token tok = parser_state.previous;

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'");
        struct expr *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'if' condition");

        struct stmt *then_stmt = parse_statement();
        if (!then_stmt)
            return NULL;

        struct stmt *else_stmt = NULL;
        if (match(TOKEN_ELSE))
            else_stmt = parse_statement();

        struct stmt *stmt = stmt_new(STMT_IF, tok);
        stmt->if_stmt.condition = cond;
        stmt->if_stmt.then_stmt = then_stmt;
        stmt->if_stmt.else_stmt = else_stmt;
        return stmt;
    }

    if (match(TOKEN_FOR)) {
        struct token tok = parser_state.previous;

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'for'");

        struct for_init *init = NULL;

        if (is_declaration_start(parser_state.current.type)) {
            init = calloc(1, sizeof(struct for_init));
            init->is_decl = true;
            init->decls = parse_declaration();
        } else if (!match(TOKEN_SEMICOLON)) {
            init = calloc(1, sizeof(struct for_init));
            init->is_decl = false;
            init->expr = parse_expression(PREC_ASSIGNMENT);
            if (!init->expr)
                return NULL;

            consume(TOKEN_SEMICOLON, "Expected ';' after for-init expression");
        }

        struct expr *cond = NULL;
        if (!match(TOKEN_SEMICOLON)) {
            cond = parse_expression(PREC_ASSIGNMENT);
            if (!cond)
                return NULL;

            consume(TOKEN_SEMICOLON, "Expected ';' after for-condition");
        }

        struct expr *post = NULL;
        if (!check(TOKEN_RIGHT_PAREN)) {
            post = parse_expression(PREC_ASSIGNMENT);
            if (!post)
                return NULL;
        }

        consume(TOKEN_RIGHT_PAREN, "Expected ')' after for clauses");

        struct stmt *body = parse_statement();
        if (!body)
            return NULL;

        struct stmt *stmt = stmt_new(STMT_FOR, tok);
        stmt->for_stmt.init = init;
        stmt->for_stmt.condition = cond;
        stmt->for_stmt.post = post;
        stmt->for_stmt.body = body;
        return stmt;
    }

    if (match(TOKEN_WHILE)) {
        struct token tok = parser_state.previous;

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'");
        struct expr *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'while' condition");

        struct stmt *body = parse_statement();
        if (!body)
            return NULL;

        struct stmt *stmt = stmt_new(STMT_WHILE, tok);
        stmt->while_stmt.condition = cond;
        stmt->while_stmt.body = body;
        return stmt;
    }

    if (match(TOKEN_DO)) {
        struct token tok = parser_state.previous;

        struct stmt *body = parse_statement();
        if (!body)
            return NULL;

        consume(TOKEN_WHILE, "Expected 'while' after 'do' body");
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'");
        struct expr *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after do-while condition");
        consume(TOKEN_SEMICOLON, "Expected ';' after do-while");

        struct stmt *stmt = stmt_new(STMT_DOWHILE, tok);
        stmt->dowhile_stmt.condition = cond;
        stmt->dowhile_stmt.body = body;
        return stmt;

    }

    if (match(TOKEN_CASE)) {
        struct token tok = parser_state.previous;

        struct expr *value = parse_expression(PREC_ASSIGNMENT);
        if (!value)
            return NULL;

        consume(TOKEN_COLON, "Expected ':' after case value");

        struct stmt *stmt = stmt_new(STMT_CASE, tok);
        stmt->case_stmt.value = value;
        stmt->case_stmt.items = parse_case_default_items();
        return stmt;
    }

    if (match(TOKEN_DEFAULT)) {
        struct token tok = parser_state.previous;

        consume(TOKEN_COLON, "Expected ':' after 'default'");

        struct stmt *stmt = stmt_new(STMT_DEFAULT, tok);
        stmt->default_stmt.items = parse_case_default_items();
        return stmt;
    }

    if (match(TOKEN_SWITCH)) {
        struct token tok = parser_state.previous;
        
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'switch'");
        struct expr *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'switch' condition");

        struct stmt *body = parse_statement();
        if (!body)
            return NULL;

        struct stmt *stmt = stmt_new(STMT_SWITCH, tok);
        stmt->switch_stmt.condition = cond;
        stmt->switch_stmt.body = body;
        return stmt;
    }

    if (match(TOKEN_BREAK)) {
        struct token tok = parser_state.previous;
        consume(TOKEN_SEMICOLON, "Expected ';' after 'break'");
        return stmt_new(STMT_BREAK, tok);
    }

    if (match(TOKEN_CONTINUE)) {
        struct token tok = parser_state.previous;
        consume(TOKEN_SEMICOLON, "Expected ';' after 'continue'");
        return stmt_new(STMT_CONTINUE, tok);
    }

    if (match(TOKEN_GOTO)) {
        struct token tok = parser_state.previous;
        
        consume(TOKEN_IDENTIFIER, "Expected label after 'goto'");
        struct token label = parser_state.previous;

        consume(TOKEN_SEMICOLON, "Expected ';' after 'goto' statement");

        struct stmt *stmt = stmt_new(STMT_GOTO, tok);
        stmt->goto_stmt.label = label;
        return stmt;
    }

    if (match(TOKEN_SEMICOLON))
        return stmt_new(STMT_NULL, parser_state.previous);

    /*
     * Either an expression statement or
     * label (identifier: statement)
     */
    struct expr *expr = parse_expression(PREC_ASSIGNMENT);
    if (!expr)
        return NULL;

    if (expr->kind == EXPR_IDENTIFIER && match(TOKEN_COLON)) {
        struct stmt *labeled = parse_statement();

        struct stmt *stmt = stmt_new(STMT_LABEL, expr->tok);
        stmt->label_stmt.name = expr->identifier.name;
        stmt->label_stmt.stmt = labeled;
        return stmt;
    }

    consume(TOKEN_SEMICOLON, "Expected ';' after expression-statement");

    struct stmt *stmt = stmt_new(STMT_EXPR, parser_state.previous);
    stmt->expr_stmt.expr = expr;
    return stmt;
}

static struct decl *parse_declarator_from_specs(struct decl_specs *specs,
                                                bool allows_abstract_name);

static struct decl_specs parse_decl_specs(void)
{
    struct decl_specs specs = {0};
    specs.storage_class = SC_NONE;

    bool saw_storage = false;
    bool saw_type = false;

    while (is_declaration_start(parser_state.current.type)) {
        if (is_storage_class_specifier(parser_state.current.type)) {
            if (saw_storage)
                error(&parser_state.current, "Multiple storage-class specifiers");

            saw_storage = true;
            specs.storage_tok = parser_state.current;

            if (parser_state.current.type == TOKEN_STATIC)
                specs.storage_class = SC_STATIC;
            else if (parser_state.current.type == TOKEN_EXTERN)
                specs.storage_class = SC_EXTERN;
            else if (parser_state.current.type == TOKEN_AUTO)
                specs.storage_class = SC_AUTO;
            else if (parser_state.current.type == TOKEN_REGISTER)
                specs.storage_class = SC_REGISTER;

            advance();
        } else if (is_type_specifier(parser_state.current.type)) {
            if (saw_type)
                error(&parser_state.current, "Multiple type specifiers");

            saw_type = true;
            specs.type_tok = parser_state.current;

            if (parser_state.current.type == TOKEN_INT)
                specs.base_type = type_int();
            else if (parser_state.current.type == TOKEN_VOID)
                specs.base_type = type_void();

            advance();
        }
    }

    if (!saw_type) {
        error(&parser_state.current, "Expected declaration type");
        specs.base_type = type_int();
    }

    return specs;
}

static struct decl *parse_parameter_declaration(void)
{
    struct decl_specs specs = parse_decl_specs();

    struct decl *d = parse_declarator_from_specs(&specs, true);
    if (!d)
        return NULL;

    d->is_parameter = true;

    return d;
}

static struct decl *parse_declarator_from_specs(struct decl_specs *specs,
                                                bool allows_abstract_name)
{
    struct token name = {0};

    if (check(TOKEN_IDENTIFIER)) {
        advance();
        name = parser_state.previous;
    } else if (!allows_abstract_name) {
        consume(TOKEN_IDENTIFIER, "Expected declaration identifier");
        name = parser_state.previous;
    }

    if (match(TOKEN_LEFT_PAREN)) {
        struct decl *params_head = NULL;
        struct decl *params_tail = NULL;

        int param_count = 0;
        bool has_prototype = true;

        if (match(TOKEN_RIGHT_PAREN)) {
            // int f() -> not a prototype
            has_prototype = false;
        } else if (match(TOKEN_VOID)) {
            consume(TOKEN_RIGHT_PAREN, "'void' must be the only parameter");
        } else {
            do {
                struct decl *param = parse_parameter_declaration();
                if (!param)
                    return NULL;

                param_count++;
                LIST_APPEND(params_head, params_tail, param);
            } while (match(TOKEN_COMMA));

            consume(TOKEN_RIGHT_PAREN, "Expected ')' after parameter list");
        }

        struct decl *d = decl_new(DECL_FUNCTION, name);
        d->storage_class = specs->storage_class;
        d->func.params = params_head;
        d->ty = type_function(specs->base_type, params_head, param_count, has_prototype);
        return d;
    }

    struct decl *d = decl_new(DECL_VAR, name);
    d->storage_class = specs->storage_class;
    d->ty = specs->base_type;

    return d;
}

static struct decl *parse_init_declarator(struct decl_specs *specs)
{
    struct decl *d = parse_declarator_from_specs(specs, false);
    if (!d)
        return NULL;

    if (match(TOKEN_EQUAL)) {
        if (d->kind == DECL_FUNCTION) {
            error(&d->name, "Function declaration cannot have an initializer");
            return NULL;
        }

        d->var.init = parse_expression(PREC_ASSIGNMENT);
        if (!d->var.init)
            return NULL;
    }

    return d;
}

static struct decl *parse_declaration(void)
{
    struct decl_specs specs = parse_decl_specs();

    struct decl *head = NULL;
    struct decl *tail = NULL;

    do {
        struct decl *d = parse_init_declarator(&specs);
        if (!d)
            return NULL;

        LIST_APPEND(head, tail, d);
    } while (match(TOKEN_COMMA));

    consume(TOKEN_SEMICOLON, "Expected ';' after declaration");
    return head;
}

static struct block_item *parse_block_item(void)
{
    if (is_declaration_start(parser_state.current.type)) {
        struct decl *decls = parse_declaration();
        if (!decls)
            return NULL;

        struct block_item *item = block_item_new(BLOCK_ITEM_DECL, parser_state.current);

        item->decls = decls;
        return item;
    }

    struct stmt *stmt = parse_statement();
    if (!stmt)
        return NULL;

    struct block_item *item = block_item_new(BLOCK_ITEM_STMT, parser_state.current);
    item->stmt = stmt;
    return item;
}

static struct stmt *parse_block_after_lbrace()
{
    struct stmt *block = stmt_new(STMT_BLOCK, parser_state.previous);
    struct block_item *tail = NULL;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        struct block_item *item = parse_block_item();

        if (!item || parser_state.panic_mode) {
            synchronize_block_item();
            continue;
        }

        LIST_APPEND(block->block.items, tail, item);
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after compound statement");

    return block;
}

static struct decl *parse_external_declaration(void)
{
    struct decl_specs specs = parse_decl_specs();
    struct decl *first = parse_init_declarator(&specs);
    if (!first)
        return NULL;

    if (first->kind == DECL_FUNCTION && match(TOKEN_LEFT_BRACE)) {
        first->is_definition = true;
        first->func.body = parse_block_after_lbrace();

        return first;
    }

    struct decl *head = first;
    struct decl *tail = first;

    while (match(TOKEN_COMMA)) {
        struct decl *d = parse_init_declarator(&specs);

        if (!d)
            return NULL;

        LIST_APPEND(head, tail, d);
    }

    consume(TOKEN_SEMICOLON, "Expected ';' after external declaration");
    return head;
}

struct ast_program *parse_translation_unit(const char *source)
{
    parser_state = (struct parser){0};

    lexer_init(source);
    advance();

    struct ast_program *program = calloc(1, sizeof(struct ast_program));
    struct decl *tail = NULL;

    while (!check(TOKEN_EOF)) {
        if (!is_declaration_start(parser_state.current.type)) {
            error(&parser_state.current, "Expected external declaration");
            synchronize_translation_unit();
            continue;
        }

        struct decl *decls = parse_external_declaration();

        if (!decls || parser_state.panic_mode) {
            synchronize_translation_unit();
            continue;
        }

        for (struct decl *decl = decls; decl; ) {
            struct decl *next = decl->next;
            decl->next = NULL;

            LIST_APPEND(program->decls, tail, decl);

            decl = next;
        }
    }

    return parser_state.had_error ? NULL : program;
}
