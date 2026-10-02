#include "parser.h"
#include "ast.h"
#include "diagnostics.h"
#include "lexer.h"
#include "type.h"
#include "base/vector.h"

#include <stdlib.h>
#include <errno.h>

typedef struct {
    token_t current;
    token_t previous;
    bool panic;
} parser_t;

typedef enum {
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
} precedence_kind;

typedef ast_expr_t *(*prefix_parse_fn)(void);
typedef ast_expr_t *(*infix_parse_fn)(ast_expr_t *left);

typedef struct {
    prefix_parse_fn prefix;
    infix_parse_fn infix;
    precedence_kind prec;
} parse_rule_t;

typedef struct {
    type_t *ty;
    storage_class sc;
    token_t tok;
} decl_specs_t;

static parser_t parser;

static void
error(const token_t *tok, const char *message)
{
    if (parser.panic)
        return;

    parser.panic = true;
    diagnostics_error(tok, "%s", message);
}

static void
advance(void)
{
    parser.previous = parser.current;

    for (;;) {
        parser.current = lexer_next_token();

        if (parser.current.kind != TOKEN_ERROR)
            break;

        error(&parser.current, "Unexpected character");
    }
}

static bool
check(token_kind kind)
{
    return parser.current.kind == kind;
}

static bool
match(token_kind kind)
{
    if (!check(kind))
        return false;

    advance();
    return true;
}

static void
consume(token_kind kind, const char *message)
{
    if (check(kind)) {
        advance();
        return;
    }

    error(&parser.current, message);
}

static bool
is_type_specifier(token_kind kind)
{
    return kind == TOKEN_INT ||
        kind == TOKEN_LONG ||
        kind == TOKEN_VOID;
}

static bool
is_storage_class_specifier(token_kind kind)
{
    return kind == TOKEN_STATIC ||
        kind == TOKEN_EXTERN ||
        kind == TOKEN_AUTO ||
        kind == TOKEN_REGISTER;
}

static bool
is_declaration_start(token_kind kind)
{
    return is_type_specifier(kind) ||
        is_storage_class_specifier(kind);
}

static storage_class
storage_class_from_token(token_kind kind)
{
    switch (kind) {
        case TOKEN_EXTERN:
            return STORAGE_CLASS_EXTERN;
        case TOKEN_STATIC:
            return STORAGE_CLASS_STATIC;
        case TOKEN_AUTO:
            return STORAGE_CLASS_AUTO;
        case TOKEN_REGISTER:
            return STORAGE_CLASS_REGISTER;
        default:
            return STORAGE_CLASS_NONE;
    }
}

static void
synchronize_translation_unit(void)
{
    parser.panic = false;

    while (parser.current.kind != TOKEN_EOF) {
        advance();

        if (is_declaration_start(parser.current.kind))
            return;
    }
}

static void
synchronize_block_item(void)
{
    parser.panic = false;

    while (parser.current.kind != TOKEN_EOF) {
        if (parser.previous.kind == TOKEN_SEMICOLON)
            return;

        if (is_declaration_start(parser.current.kind))
            return;

        switch (parser.current.kind) {
            case TOKEN_RETURN:
            case TOKEN_IF:
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


static ast_expr_t *parse_expression(precedence_kind prec);
static const parse_rule_t *get_parse_rule(token_kind kind);
static type_t *parse_type_name(void);

static ast_stmt_t *parse_statement(void);
static ast_stmt_t *parse_block(void);

static ast_stmt_t *parse_declaration(bool file_scope);
static ast_decl_t *parse_declarator(type_t *base_ty, bool allow_abstract);

/*
 * Expression parsing
 */

static ast_expr_t *
constant(void)
{
    token_t tok = parser.previous;

    errno = 0;
    char *end = nullptr;
    uint64_t value = strtoll(tok.start, &end, 10);
    if (errno == ERANGE || value > INT64_MAX) {
        error(&tok, "Literal is too large to represent as int or long");
        value = INT64_MAX;
    }

    expr_kind kind =
        (tok.kind == TOKEN_INT_CONSTANT && value <= INT32_MAX)
            ? EXPR_INT_CONSTANT
            : EXPR_LONG_CONSTANT;

    ast_expr_t *expr = ast_expr_new(kind, tok);
    expr->constant_value = value;
    return expr;
}

static ast_expr_t *
identifier(void)
{
    ast_expr_t *expr = ast_expr_new(EXPR_IDENTIFIER, parser.previous);
    expr->identifier.name = parser.previous;
    return expr;
}


static ast_expr_t *
unary(void)
{
    token_t op = parser.previous;
    ast_expr_t *operand = parse_expression(PREC_UNARY);
    if (!operand)
        return nullptr;

    ast_expr_t *expr = ast_expr_new(EXPR_UNARY, op);
    expr->unary.op = op;
    expr->unary.operand = operand;
    return expr;
}

static ast_expr_t *
pre(void)
{
    token_t op = parser.previous;
    ast_expr_t *operand = parse_expression(PREC_UNARY);
    if (!operand)
        return nullptr;

    ast_expr_t *expr = ast_expr_new(EXPR_PRE, op);
    expr->unary.op = op;
    expr->unary.operand = operand;
    return expr;
}

static ast_expr_t *
grouping_or_cast(void)
{
    token_t lparen = parser.previous;

    if (is_type_specifier(parser.current.kind)) {
        type_t *target_ty = parse_type_name();

        consume(TOKEN_RIGHT_PAREN, "Expected ')' after type name");

        ast_expr_t *operand = parse_expression(PREC_UNARY);
        if (!operand)
            return nullptr;

        ast_expr_t *expr = ast_expr_new(EXPR_CAST, lparen);
        expr->cast.target_ty = target_ty;
        expr->cast.operand = operand;
        return expr;
    }

    ast_expr_t *expr = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression");
    return expr;
}

static ast_expr_t *
binary(ast_expr_t *left)
{
    token_t op = parser.previous;
    const parse_rule_t *rule = get_parse_rule(op.kind);

    ast_expr_t *right = parse_expression(rule->prec + 1);
    if (!right)
        return nullptr;

    ast_expr_t *expr = ast_expr_new(EXPR_BINARY, op);
    expr->binary.op = op;
    expr->binary.left = left;
    expr->binary.right = right;
    return expr;
}

static ast_expr_t *
assignment(ast_expr_t *left)
{
    token_t op = parser.previous;
    ast_expr_t *right = parse_expression(PREC_ASSIGNMENT);
    if (!right)
        return nullptr;

    ast_expr_t *expr = ast_expr_new(EXPR_ASSIGNMENT, op);
    expr->assignment.op = op;
    expr->assignment.lvalue = left;
    expr->assignment.rvalue = right;
    return expr;
}

static ast_expr_t *
post(ast_expr_t *left)
{
    token_t op = parser.previous;

    ast_expr_t *expr = ast_expr_new(EXPR_POST, op);
    expr->unary.op = op;
    expr->unary.operand = left;
    return expr;
}

static ast_expr_t *
ternary(ast_expr_t *left)
{
    token_t tok = parser.previous; // ? token
    
    ast_expr_t *then_expr = parse_expression(PREC_ASSIGNMENT);
    if (!then_expr)
        return nullptr;

    consume(TOKEN_COLON, "Expected ':' after conditional expression");
    ast_expr_t *else_expr = parse_expression(PREC_TERNARY);
    if (!else_expr)
        return nullptr;

    ast_expr_t *expr = ast_expr_new(EXPR_CONDITIONAL, tok);
    expr->conditional.condition = left;
    expr->conditional.then_expr = then_expr;
    expr->conditional.else_expr = else_expr;
    return expr;
}

static ast_expr_t *
call(ast_expr_t *left)
{
    token_t tok = parser.previous;

    ast_expr_t *expr = ast_expr_new(EXPR_CALL, tok);
    expr->call.callee = left;
    LIST_INIT(&expr->call.args);

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            ast_expr_t *arg = parse_expression(PREC_ASSIGNMENT);
            if (!arg)
                return nullptr;

            LIST_APPEND(&expr->call.args, arg);
        } while(match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expected ')' after arguments");
    return expr;
}

static const parse_rule_t parse_rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping_or_cast, call, PREC_POSTFIX},
    [TOKEN_RIGHT_PAREN]   = {nullptr, nullptr, PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {nullptr, nullptr, PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {nullptr, nullptr, PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {nullptr, nullptr, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {nullptr, nullptr, PREC_NONE},
    [TOKEN_SEMICOLON]     = {nullptr, nullptr, PREC_NONE},
    [TOKEN_COLON]         = {nullptr, nullptr, PREC_NONE},
    [TOKEN_COMMA]         = {nullptr, nullptr, PREC_NONE},
    [TOKEN_QUESTION_MARK] = {nullptr, ternary, PREC_TERNARY},

    [TOKEN_PLUS]          = {unary, binary, PREC_TERM},
    [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
    [TOKEN_STAR]          = {nullptr, binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {nullptr, binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {nullptr, binary, PREC_FACTOR},
    
    [TOKEN_AND_AND]       = {nullptr, binary, PREC_AND},
    [TOKEN_OR_OR]         = {nullptr, binary, PREC_OR},

    [TOKEN_BANG]          = {unary, nullptr, PREC_NONE},
    [TOKEN_TILDE]         = {unary, nullptr, PREC_NONE},

    [TOKEN_CARET]         = {nullptr, binary, PREC_BITWISE_XOR},
    [TOKEN_OR]            = {nullptr, binary, PREC_BITWISE_OR},
    [TOKEN_AND]           = {nullptr, binary, PREC_BITWISE_AND},

    [TOKEN_MINUS_MINUS]   = {pre, post, PREC_POSTFIX},
    [TOKEN_PLUS_PLUS]     = {pre, post, PREC_POSTFIX},

    [TOKEN_EQUAL]         = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_PLUS_EQUAL]    = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_MINUS_EQUAL]   = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_STAR_EQUAL]    = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_SLASH_EQUAL]   = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_PERCENT_EQUAL] = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_AND_EQUAL]     = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_OR_EQUAL]      = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_CARET_EQUAL]   = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_LESS_LESS_EQUAL] = {nullptr, assignment, PREC_ASSIGNMENT},
    [TOKEN_GREATER_GREATER_EQUAL] = {nullptr, assignment, PREC_ASSIGNMENT},

    [TOKEN_EQUAL_EQUAL]   = {nullptr, binary, PREC_EQUALITY},
    [TOKEN_BANG_EQUAL]    = {nullptr, binary, PREC_EQUALITY},
    [TOKEN_LESS]          = {nullptr, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {nullptr, binary, PREC_COMPARISON},
    [TOKEN_LESS_LESS]     = {nullptr, binary, PREC_BITWISE_SHIFT},
    [TOKEN_GREATER]       = {nullptr, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {nullptr, binary, PREC_COMPARISON},
    [TOKEN_GREATER_GREATER] = {nullptr, binary, PREC_BITWISE_SHIFT},

    [TOKEN_IDENTIFIER]    = {identifier, nullptr, PREC_NONE},
    [TOKEN_INT_CONSTANT]  = {constant, nullptr, PREC_NONE},
    [TOKEN_LONG_CONSTANT] = {constant, nullptr, PREC_NONE},

    [TOKEN_INT]           = {nullptr, nullptr, PREC_NONE},
    [TOKEN_LONG]          = {nullptr, nullptr, PREC_NONE},
    [TOKEN_VOID]          = {nullptr, nullptr, PREC_NONE},
    [TOKEN_STATIC]        = {nullptr, nullptr, PREC_NONE},
    [TOKEN_EXTERN]        = {nullptr, nullptr, PREC_NONE},
    [TOKEN_AUTO]          = {nullptr, nullptr, PREC_NONE},
    [TOKEN_REGISTER]      = {nullptr, nullptr, PREC_NONE},
    [TOKEN_RETURN]        = {nullptr, nullptr, PREC_NONE},
    [TOKEN_IF]            = {nullptr, nullptr, PREC_NONE},
    [TOKEN_ELSE]          = {nullptr, nullptr, PREC_NONE},
    [TOKEN_FOR]           = {nullptr, nullptr, PREC_NONE},
    [TOKEN_WHILE]         = {nullptr, nullptr, PREC_NONE},
    [TOKEN_DO]            = {nullptr, nullptr, PREC_NONE},
    [TOKEN_BREAK]         = {nullptr, nullptr, PREC_NONE},
    [TOKEN_CONTINUE]      = {nullptr, nullptr, PREC_NONE},
    [TOKEN_SWITCH]        = {nullptr, nullptr, PREC_NONE},
    [TOKEN_CASE]          = {nullptr, nullptr, PREC_NONE},
    [TOKEN_DEFAULT]       = {nullptr, nullptr, PREC_NONE},
    [TOKEN_GOTO]          = {nullptr, nullptr, PREC_NONE},

    [TOKEN_ERROR]         = {nullptr, nullptr, PREC_NONE},
    [TOKEN_EOF]           = {nullptr, nullptr, PREC_NONE},
};

static const parse_rule_t *
get_parse_rule(token_kind kind)
{
    return &parse_rules[kind];
}

static ast_expr_t *
parse_expression(precedence_kind prec)
{
    advance();
    prefix_parse_fn prefix_fn =
        get_parse_rule(parser.previous.kind)->prefix;
    if (!prefix_fn) {
        error(&parser.previous, "Expected expression");
        return nullptr;
    }

    ast_expr_t *left = prefix_fn();

    while (prec <= get_parse_rule(parser.current.kind)->prec) {
        advance();
        infix_parse_fn infix_fn =
            get_parse_rule(parser.current.kind)->infix;
        left = infix_fn(left);
    }

    return left;
}

/*
 * Statement parsing
 */

static ast_stmt_t *
parse_statement(void)
{
    if (is_declaration_start(parser.current.kind)) {
        error(&parser.current, "Expected statement, not declaration.");
        return nullptr;
    }

    if (check(TOKEN_LEFT_BRACE)) {
        return parse_block();
    }

    if (match(TOKEN_SEMICOLON)) {
        return ast_stmt_new(STMT_NULL, parser.previous);
    }

    if (match(TOKEN_RETURN)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_RETURN, parser.previous);

        if (!check(TOKEN_SEMICOLON))
            stmt->return_stmt.expr = parse_expression(PREC_ASSIGNMENT);

        consume(TOKEN_SEMICOLON, "Expected ';' after 'return'");
        return stmt;
    }

    if (match(TOKEN_IF)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_IF, parser.previous);

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'");
        stmt->if_stmt.condition = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'if'");

        stmt->if_stmt.then_stmt = parse_statement();
        if (match(TOKEN_ELSE))
            stmt->if_stmt.else_stmt = parse_statement();

        return stmt;
    }

    if (match(TOKEN_WHILE)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_WHILE, parser.previous);

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'");
        stmt->loop.condition = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'while'");

        stmt->loop.body = parse_statement();
        return stmt;
    }

    if (match(TOKEN_DO)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_DOWHILE, parser.previous);

        stmt->loop.body = parse_statement();

        consume(TOKEN_WHILE, "Expected 'while' after 'do' body");
        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'");
        stmt->loop.condition = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'while' condition");
        consume(TOKEN_SEMICOLON, "Expected ';' after do-while statement");

        return stmt;
    }

    if (match(TOKEN_FOR)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_FOR, parser.previous);

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'for'");
        if (is_declaration_start(parser.current.kind)) {
            stmt->loop.init = parse_declaration(false);
        } else if (!match(TOKEN_SEMICOLON)) {
            ast_stmt_t *init = ast_stmt_new(STMT_EXPR, parser.previous);
            init->expr.expr = parse_expression(PREC_ASSIGNMENT);
            consume(TOKEN_SEMICOLON, "Expected ';' after 'for' initializer");
            stmt->loop.init = init;
        }

        if (!check(TOKEN_SEMICOLON))
            stmt->loop.condition = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_SEMICOLON, "Expected ';' after 'for' condition");
        
        if (!check(TOKEN_SEMICOLON))
            stmt->loop.post = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_SEMICOLON, "Expected ';' after 'for'");

        stmt->loop.body = parse_statement();
        return stmt;
    }

    if (match(TOKEN_SWITCH)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_SWITCH, parser.previous);

        consume(TOKEN_LEFT_PAREN, "Expected '(' after 'switch'");
        stmt->switch_stmt.condition = parse_expression(PREC_ASSIGNMENT);
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'switch' condition");

        stmt->switch_stmt.body = parse_statement();
        return stmt;
    }

    if (match(TOKEN_CASE)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_CASE, parser.previous);

        stmt->case_stmt.expr = parse_expression(PREC_TERNARY);
        consume(TOKEN_COLON, "Expected ':' after 'case' value");

        stmt->case_stmt.stmt = parse_statement();
        return stmt;
    }

    if (match(TOKEN_DEFAULT)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_DEFAULT, parser.previous);

        consume(TOKEN_COLON, "Expected ':' after 'default'");
        
        stmt->default_stmt.stmt = parse_statement();
        return stmt;
    }

    if (match(TOKEN_GOTO)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_GOTO, parser.previous);

        stmt->goto_stmt.label = parser.current;
        consume(TOKEN_IDENTIFIER, "Expected label name after 'goto'");
        consume(TOKEN_SEMICOLON, "Expected ';' after 'goto' statement");
        return stmt;
    }

    if (match(TOKEN_BREAK)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_BREAK, parser.previous);
        consume(TOKEN_SEMICOLON, "Expected ';' after 'break'");
        return stmt;
    }

    if (match(TOKEN_CONTINUE)) {
        ast_stmt_t *stmt = ast_stmt_new(STMT_CONTINUE, parser.previous);
        consume(TOKEN_SEMICOLON, "Expected ';' after 'continue'");
        return stmt;
    }

    if (check(TOKEN_IDENTIFIER) &&
        lexer_peek_token().kind == TOKEN_COLON) {
        token_t name = parser.current;
        advance(); // ident
        advance(); // ':'
                   
        ast_stmt_t *stmt = ast_stmt_new(STMT_LABEL, name);
        stmt->label_stmt.label = name;
        stmt->label_stmt.stmt = parse_statement();
        return stmt;
    }

    /* Expression statement */
    ast_stmt_t *stmt = ast_stmt_new(STMT_EXPR, parser.previous);
    stmt->expr.expr = parse_expression(PREC_ASSIGNMENT);
    consume(TOKEN_SEMICOLON, "Expected ';' after expression statement");
    return stmt;
}

static ast_stmt_t *
parse_block_item(void)
{
    if (is_declaration_start(parser.current.kind))
        return parse_declaration(false);

    return parse_statement();
}

static ast_stmt_t *
parse_block(void)
{
    ast_stmt_t *block = ast_stmt_new(STMT_BLOCK, parser.previous);
    consume(TOKEN_LEFT_BRACE, "Expected '{'");

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        const char *before = parser.current.start;

        ast_stmt_t *item = parse_block_item();
        LIST_APPEND(&block->block.items, item);

        if (parser.panic) {
            /* Makes sure synchronization isn't
             * in an endless loop */
            if (parser.current.start == before)
                advance();

            synchronize_block_item();
        }
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' at the end of block");
    return block;
}

/*
 * Declaration parsing
 */

static decl_specs_t
parse_declaration_specs(void)
{
    decl_specs_t specs = {
        .tok = parser.current
    };

    int n_void = 0, n_int = 0, n_long = 0;
    bool saw_sc = false;

    while (is_declaration_start(parser.current.kind)) {
        token_t tok = parser.current;
        advance();
        
        switch (tok.kind) {
            case TOKEN_INT:
                n_int++;
                break;
            case TOKEN_LONG:
                n_long++;
                break;
            case TOKEN_VOID:
                n_void++;
                break;
            default:
                if (saw_sc)
                    error(&tok,
                        "Multiple storage classes in declaration");
                saw_sc = true;
                specs.sc = storage_class_from_token(tok.kind);
        }
    }

    if (n_void == 1 && n_int == 0 && n_long == 0)
        specs.ty = type_void();
    else if (n_long == 1 && n_int <= 1 && n_void == 0)
        specs.ty = type_long();
    else if (n_int == 1 && n_long == 0 && n_void == 0)
        specs.ty = type_int();
    else {
        error(&specs.tok, n_void + n_int + n_long == 0 
                ? "Expected type specifier"
                : "Invalid combination of type specifiers");
        specs.ty = type_int();
    }

    return specs;
}

static type_t *
parse_type_name(void)
{
    decl_specs_t specs = parse_declaration_specs();

    if (specs.sc != STORAGE_CLASS_NONE)
        error(&specs.tok, "Storage class not allowed in type name");

    return specs.ty;
}

static ast_decl_t *
parse_parameter_declaration(void)
{
    decl_specs_t specs = parse_declaration_specs();
    
    ast_decl_t *param = parse_declarator(specs.ty, true);
    param->sc = specs.sc; /* Sema: only 'register' allowed */
    param->is_parameter = true;
    return param;
}


static void
parse_parameter_list(ast_decl_t *fn, vector *param_types)
{
    if (match(TOKEN_RIGHT_PAREN))
        return;

    if (check(TOKEN_VOID) &&
        lexer_peek_token().kind == TOKEN_RIGHT_PAREN) {
        advance(); // 'void'
        advance(); // ')'
        return;
    }

    do {
        ast_decl_t *param = parse_parameter_declaration();

        LIST_APPEND(&fn->function.params, param);
        vector_push(param_types, &param->ty);
    } while(match(TOKEN_COMMA));

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after parameter list");
}

static ast_decl_t *
parse_declarator(type_t *base_ty, bool allow_abstract)
{
    token_t name = parser.current;

    if (match(TOKEN_IDENTIFIER)) {
        name = parser.previous;
    } else {
        if (!allow_abstract) {
            error(&name,
                    "Expected identifier in declaration");
        }

        name.len = 0;
    }

    if (!match(TOKEN_LEFT_PAREN)) {
        ast_decl_t *decl = ast_decl_new(DECL_OBJECT, name);
        decl->ty = base_ty;
        return decl;
    }

    ast_decl_t *decl = ast_decl_new(DECL_FUNCTION, name);

    vector param_types;
    VECTOR_INIT(&param_types, type_t *);
    parse_parameter_list(decl, &param_types);

    decl->ty = type_function(base_ty, param_types, false);
    return decl;
}

static ast_decl_t *
parse_init_declarator(decl_specs_t *specs)
{
    ast_decl_t *decl = parse_declarator(specs->ty, false);
    decl->sc = specs->sc;

    if (match(TOKEN_EQUAL)) {
        token_t equal = parser.previous;
        ast_expr_t *init = parse_expression(PREC_ASSIGNMENT);

        if (decl->kind == DECL_FUNCTION) {
            error(&equal,
                    "Function declaration cannot have an initializer");
        } else {
            decl->object.init = init;
        }
    }

    return decl;
}

/* One declaration: int a, f();
 * Returns STMT_DECL with every decl */
static ast_stmt_t *
parse_declaration(bool file_scope)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_DECL, parser.previous);
    decl_specs_t specs = parse_declaration_specs();

    ast_decl_t *first = parse_init_declarator(&specs);
    LIST_APPEND(&stmt->decl.decls, first);

    if (first->kind == DECL_FUNCTION && check(TOKEN_LEFT_BRACE)) {
        if (!file_scope) {
            error(&parser.current,
                    "Function definition not allowed at block scope");
        }

        first->function.body = parse_block();
        return stmt;
    }

    while (match(TOKEN_COMMA)) {
        ast_decl_t *decl = parse_init_declarator(&specs);
        LIST_APPEND(&stmt->decl.decls, decl);
    }

    consume(TOKEN_SEMICOLON, "Expected ';' after declaration");
    return stmt;
}

ast_program_t *
parse_translation_unit(
    const char *source,
    const char *filename
)
{
    parser = (parser_t){};

    lexer_init(source, filename);
    advance();

    ast_program_t *program = ast_program_new();

    while (!check(TOKEN_EOF)) {
        const char *before = parser.current.start;

        if (!is_declaration_start(parser.current.kind)) {
            error(&parser.current, "Expected declaration");
        } else {
            ast_stmt_t *decls = parse_declaration(true);
            LIST_CONCAT(&program->decls, &decls->decl.decls);
        }

        if (parser.panic) {
            if (parser.current.start == before)
                advance();

            synchronize_translation_unit();
        }
    }

    return diagnostics_had_error() ?
        nullptr :
        program;
}
