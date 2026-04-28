#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

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

typedef struct expr* (*prefix_parse_fn)(void);
typedef struct expr* (*infix_parse_fn)(struct expr *);

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

struct decl_specs {
    struct type *base_type;
    enum storage_class storage_class;
    struct token storage_tok;
};

struct declarator {
    struct token name;
    struct type *ty;
    struct decl *params;
    bool is_function;
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

static bool is_declaration_specifier(enum token_type type)
{
    switch (type) {
        case TOKEN_VOID:
        case TOKEN_INT:
        case TOKEN_STATIC:
        case TOKEN_EXTERN:
        case TOKEN_AUTO:
            return true;
        default:
            return false;
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
            case TOKEN_VOID:
            case TOKEN_STATIC:
            case TOKEN_EXTERN:
            case TOKEN_AUTO:
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
            case TOKEN_LEFT_BRACE:
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
        if (is_declaration_specifier(parser_state.current.type))
            return;
        
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
    struct token tok = parser_state.previous;
    
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
    expr->call.calle = left;
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

    [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
    [TOKEN_PLUS]          = {unary, binary, PREC_TERM},
    [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL, binary, PREC_FACTOR},

    [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
    [TOKEN_TILDE]         = {unary, NULL, PREC_UNARY},

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

/* Core Pratt dispatch: parse an expression at given precedence level.
 * Calls prefix rule for current token, then keeps consuming
 * infix operators as long as they bind tighter than prec */
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

/* Declarations */

static void set_storage_class(struct decl_specs *specs, enum storage_class sc, struct token tok)
{
    if (specs->storage_class != SC_NONE) {
        error(&tok, "Multiple storage-class specifiers");
    }

    specs->storage_class = sc;
    specs->storage_tok = tok;
}

static struct decl_specs parse_declaration_specs(void)
{
    struct decl_specs specs = {
        .base_type = NULL,
        .storage_class = SC_NONE,
    };

    bool saw_any = false;

    while (is_declaration_specifier(parser_state.current.type)) {
        saw_any = true;

        if (match(TOKEN_INT)) {
            if (specs.base_type)
                error(&parser_state.current, "Duplicate type specifiers");
            specs.base_type = type_int();
            continue;
        }

        if (match(TOKEN_VOID)) {
            if (specs.base_type)
                error(&parser_state.current, "Duplicate type specifiers");
            specs.base_type = type_void();
            continue;
        }

        if (match(TOKEN_STATIC)) {
            set_storage_class(&specs, SC_STATIC, parser_state.previous);
            continue;
        }

        if (match(TOKEN_EXTERN)) {
            set_storage_class(&specs, SC_EXTERN, parser_state.previous);
            continue;
        }

        if (match(TOKEN_AUTO)) {
            set_storage_class(&specs, SC_AUTO, parser_state.previous);
            continue;
        }
    }
    
    if (!saw_any)
        error(&parser_state.current, "Expected declaration specifier");

    if (!specs.base_type)
        error(&parser_state.current, "Expected type specifier");

    if (!specs.base_type)
        specs.base_type = type_int();

    return specs;
}

static struct decl *parse_parameter_list(bool *has_prototype)
{
    struct decl *params_head = NULL;
    struct decl *params_tail = NULL;

    if (check(TOKEN_RIGHT_PAREN)) {
        *has_prototype = false;
        return NULL;
    }

    *has_prototype = true;

    if (match(TOKEN_VOID)) {
        if (!check(TOKEN_RIGHT_PAREN))
            error(&parser_state.previous, "'void' must be the only parameter");
        return NULL;
    }

    do {
        struct decl_specs specs = parse_declaration_specs();

        if (specs.storage_class != SC_NONE)
            error(&parser_state.previous, "Invalid storage class in parameter declaration");

        if (type_is_void(specs.base_type))
            error(&parser_state.previous, "Parameter cannot have 'void' type");

        struct token name = {0};
        if (match(TOKEN_IDENTIFIER))
            name = parser_state.previous;

        struct decl *param = decl_new(DECL_VAR, name);
        param->ty = specs.base_type;
        param->storage_class = specs.storage_class;
        param->storage_duration = SD_AUTO;
        param->linkage = LINK_NONE;
        param->is_definition = true;

        LIST_APPEND(params_head, params_tail, param);
    } while (match(TOKEN_COMMA));

    return params_head;
}

static struct declarator parse_declarator(struct type *base_type)
{
    struct declarator d = {0};

    consume(TOKEN_IDENTIFIER, "Expected declarator name");
    d.name = parser_state.previous;
    d.ty = base_type;

    if (match(TOKEN_LEFT_PAREN)) {
        bool has_prototype = false;
        struct decl *params = parse_parameter_list(&has_prototype);

        consume(TOKEN_RIGHT_PAREN, "Expected ')' after parameter list");

        d.is_function = true;
        d.params = params;
        d.ty = type_function(base_type, params, has_prototype);
    }

    return d;
}

static struct decl *finish_decl_from_declarator(struct declarator d, struct decl_specs specs)
{
    struct decl *decl;

    if (d.is_function) {
        decl = decl_new(DECL_FUNC, d.name);
        decl->ty = d.ty;
        decl->func.params = d.params;
        decl->storage_class = specs.storage_class;
        return decl;
    }

    decl = decl_new(DECL_VAR, d.name);
    decl->ty = d.ty;
    decl->storage_class = specs.storage_class;

    if (type_is_void(decl->ty))
        error(&decl->name, "Variable cannot have type 'void'");

    return decl;
}

static struct decl *parse_init_declarator(struct decl_specs specs)
{
    struct declarator d = parse_declarator(specs.base_type);
    struct decl *decl = finish_decl_from_declarator(d, specs);

    if (decl->kind == DECL_FUNC) {
        if (match(TOKEN_EQUAL))
            error(&decl->name, "Function declaration cannot have an initializer");
        return decl;
    }

    if (match(TOKEN_EQUAL)) {
        decl->var.init = parse_expression(PREC_ASSIGNMENT);
        decl->is_definition = true;
        decl->is_tentative = false;
    }
    
    return decl;
}

static struct decl *parse_declaration(void)
{
    struct decl_specs specs = parse_declaration_specs();

    struct decl *head = NULL;
    struct decl *tail = NULL;

    do {
        struct decl *decl = parse_init_declarator(specs);
        if (!decl)
            return NULL;

        LIST_APPEND(head, tail, decl);
    } while (match(TOKEN_COMMA));

    consume(TOKEN_SEMICOLON, "Expected ';' after declaration");
    return head;
}

/* Statements */

static struct stmt *parse_block_after_lbrace(struct token lbrace_tok);

static struct block_item *block_item_from_decls(struct decl *decls)
{
    struct block_item *item = calloc(1, sizeof(struct block_item));
    item->kind = BLOCK_ITEM_DECL;
    item->decls = decls;
    return item;
}

static struct block_item *block_item_from_stmt(struct stmt *stmt)
{
    struct block_item *item = calloc(1, sizeof(struct block_item));
    item->kind = BLOCK_ITEM_STMT;
    item->stmt = stmt;
    return item;
}

static struct for_init *for_init_from_decls(struct decl *decls)
{
    struct for_init *init = calloc(1, sizeof(struct for_init));
    init->is_decl = true;
    init->decls = decls;
    return init;
}

static struct for_init *for_init_from_expr(struct expr *expr)
{
    struct for_init *init = calloc(1, sizeof(struct for_init));
    init->is_decl = false;
    init->expr = expr;
    return init;
}

static struct stmt *parse_statement(void)
{
    if (match(TOKEN_LEFT_BRACE)) {
        return parse_block_after_lbrace(parser_state.previous);
    }

    if (match(TOKEN_RETURN)) {
        struct token tok = parser_state.previous;
        struct expr *expr = NULL;

        if (!check(TOKEN_SEMICOLON) && !check(TOKEN_EOF)) {
            expr = parse_expression(PREC_ASSIGNMENT);
            if (!expr)
                return NULL;
        }

        consume(TOKEN_SEMICOLON, "Expected ';' after return value");

        struct stmt *stmt = stmt_new(STMT_RETURN, tok);
        stmt->return_stmt.expr = expr;
        return stmt;
    }
    
    if (match(TOKEN_IF)) {
        struct token tok = parser_state.previous;

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'");
        struct expr *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'if' condition");

        if (check(TOKEN_ELSE) || check(TOKEN_RIGHT_BRACE) || check(TOKEN_EOF)) {
            error(&parser_state.previous, "Expected statement after 'if'");
            return NULL;
        }

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

    if (match(TOKEN_SWITCH)) {
        struct token tok = parser_state.previous;

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'switch'");
        struct expr *cond = parse_expression(PREC_ASSIGNMENT);
        if (!cond)
            return NULL;
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'switch' condition");

        struct stmt *body = parse_statement();
        if (!body)
            return NULL;

        struct stmt *stmt = stmt_new(STMT_SWITCH, tok);
        stmt->switch_stmt.condition = cond;
        stmt->switch_stmt.body = body;
        return stmt;
    }

    if (match(TOKEN_CASE)) {
        struct token tok = parser_state.previous;

        struct expr *value = parse_expression(PREC_ASSIGNMENT);
        if (!value)
            return NULL;
        consume(TOKEN_COLON, "Expected ':' after 'case'");

        struct stmt *stmts_head = NULL;
        struct stmt *stmts_tail = NULL;
        while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
                !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
            struct stmt *stmt = parse_statement();
            if (!stmt)
                return NULL;

            LIST_APPEND(stmts_head, stmts_tail, stmt);
        }

        struct stmt *stmt = stmt_new(STMT_CASE, tok);
        stmt->case_stmt.value = value;
        stmt->case_stmt.first = stmts_head;
        return stmt;
    }

    if (match(TOKEN_DEFAULT)) {
        struct token tok = parser_state.previous;

        consume(TOKEN_COLON, "Expected ':' after 'default'");

        struct stmt *stmts_head = NULL;
        struct stmt *stmts_tail = NULL;
        while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
                !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
            struct stmt *stmt = parse_statement();
            if (!stmt)
                return NULL;

            LIST_APPEND(stmts_head, stmts_tail, stmt);
        }

        struct stmt *stmt = stmt_new(STMT_DEFAULT, tok);
        stmt->default_stmt.first = stmts_head;
        return stmt;
    }

    if (match(TOKEN_FOR)) {
        struct token tok = parser_state.previous;
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'for'");

        struct for_init *init = NULL;
        if (is_declaration_specifier(parser_state.current.type)) {
            struct decl *decls = parse_declaration();
            init = for_init_from_decls(decls);
        } else if (!match(TOKEN_SEMICOLON)) {
            struct expr *expr = parse_expression(PREC_ASSIGNMENT);
            if (!expr)
                return NULL;

            consume(TOKEN_SEMICOLON, "Expected ';' after for-init");
            init = for_init_from_expr(expr);
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

        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'for' clauses");

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
        if (!body) {
            error(&parser_state.previous, "Empty 'while' loop body");
            return NULL;
        }

        struct stmt *stmt = stmt_new(STMT_WHILE, tok);
        stmt->while_stmt.condition = cond;
        stmt->while_stmt.body = body;
        return stmt;
    }

    if (match(TOKEN_DO)) {
        struct token tok = parser_state.previous;

        struct stmt *body = parse_statement();
        if (!body) {
            error(&parser_state.previous, "Empty 'do-while' loop body");
            return NULL;
        }

        consume(TOKEN_WHILE, "Expected 'while' after 'do-while' body");
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'do-while'");
        struct expr *cond = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'do-while' condition");
        consume(TOKEN_SEMICOLON, "Expected ';' after 'do-while'");

        struct stmt *stmt = stmt_new(STMT_DOWHILE, tok);
        stmt->dowhile_stmt.body = body;
        stmt->dowhile_stmt.condition = cond;
        return stmt;
    }

    if (match(TOKEN_GOTO)) {
        struct token tok = parser_state.previous;

        consume(TOKEN_IDENTIFIER, "Expected label after 'goto'");
        struct token label = parser_state.previous;

        consume(TOKEN_SEMICOLON, "Expected ';' after goto statement");

        struct stmt *stmt = stmt_new(STMT_GOTO, tok);
        stmt->goto_stmt.label = label;
        return stmt;
    }

    if (match(TOKEN_BREAK)) {
        consume(TOKEN_SEMICOLON, "Expected ';' after 'break'");
        return stmt_new(STMT_BREAK, parser_state.previous);
    }

    if (match(TOKEN_CONTINUE)) {
        consume(TOKEN_SEMICOLON, "Expected ';' after 'continue'");
        return stmt_new(STMT_CONTINUE, parser_state.previous);
    }

    if (match(TOKEN_SEMICOLON))
        return stmt_new(STMT_NULL, parser_state.previous);

    /*
     * If didn't match with any statement
     * It's either an expression-statement or goto label
     */
    struct expr *expr = parse_expression(PREC_ASSIGNMENT);
    if (!expr)
        return NULL;

    // Label
    if (expr->kind == EXPR_IDENTIFIER && match(TOKEN_COLON)) {
        struct stmt *substmt = parse_statement();
        if (!substmt)
            return NULL;

        struct stmt *stmt = stmt_new(STMT_LABEL, expr->tok);
        stmt->label_stmt.stmt = substmt;
        stmt->label_stmt.name = expr->identifier.name;
        return stmt;
    }

    // Othwerwise expression statement
    consume(TOKEN_SEMICOLON, "Expected ';' after expression statement");
    struct stmt *stmt = stmt_new(STMT_EXPR, parser_state.previous);
    stmt->expr_stmt.expr = expr;
    return stmt;
}

/* Parser a compound statement */
static struct stmt *parse_block_after_lbrace(struct token lbrace_tok)
{
    struct stmt *block = stmt_new(STMT_BLOCK, lbrace_tok);
    struct block_item *tail = NULL;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        struct block_item *item = NULL;

        if (is_declaration_specifier(parser_state.current.type)) {
            struct decl *decls = parse_declaration();
            item = block_item_from_decls(decls);
        } else {
            struct stmt *stmt = parse_statement();
            if (stmt)
                item = block_item_from_stmt(stmt);
        }

        if (!item || parser_state.panic_mode) {
            synchronize_block_item();
            continue;
        }

        LIST_APPEND(block->block.items, tail, item);
    }

    if (!parser_state.had_error)
        consume(TOKEN_RIGHT_BRACE, "Expected '}' after block");
    else
        match(TOKEN_RIGHT_BRACE);

    return block;
}

/*
 * Parses external declaration or function definition.
 * Returns a declaration node or list of declaration nodes.
 *
 * Examples:
 *   int x;
 *   int foo, bar;
 *   static int = 3;
 *   int foo(int bar) { return bar; }
 */
static struct decl *parse_external_declaration(void)
{
    struct decl_specs specs = parse_declaration_specs();
    struct declarator d = parse_declarator(specs.base_type);
    struct decl *first = finish_decl_from_declarator(d, specs);

    if (first->kind == DECL_FUNC && match(TOKEN_LEFT_BRACE)) {
        first->is_definition = true;
        first->func.body = parse_block_after_lbrace(parser_state.previous);
        return first;
    }

    if (first->kind == DECL_VAR) {
        if (match(TOKEN_EQUAL)) {
            first->var.init = parse_expression(PREC_ASSIGNMENT);
            first->is_definition = true;
            first->is_tentative = false;
        }
    }

    struct decl *head = first;
    struct decl *tail = NULL;
    while (match(TOKEN_COMMA)) {
        struct decl *decl = parse_init_declarator(specs);
        if (!decl)
            break;

        LIST_APPEND(head, tail, decl);
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
        if (!is_declaration_specifier(parser_state.current.type)) {
            error(&parser_state.current, "Expected external declaration");
            synchronize_translation_unit();
            continue;
        }

        struct decl *decls = parse_external_declaration();
        if (!decls || parser_state.panic_mode) {
            synchronize_translation_unit();
            continue;
        }

        // A declaration might be 'int foo, bar' so we need append them all
        for (struct decl *decl = decls; decl; ) {
            struct decl *next = decl->next;
            decl->next = NULL;

            LIST_APPEND(program->decls, tail, decl);

            decl = next;
        }
    }

    return parser_state.had_error ? NULL : program;
}
