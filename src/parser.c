#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "parser.h"
#include "ast.h"
#include "lexer.h"
#include "sema.h"
#include "type.h"

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

struct decl_specs {
    enum storage_class storage;
    struct type *base_type;
};

struct parsed_declarator {
    struct token name;
    struct type *type;
    struct ast_node *params;
};

struct param_list {
    struct ast_node *head;
    struct ast_node *tail;

    struct type **types;
    size_t count;

    bool is_variadic;
};

static struct parser parser_state;

static struct ast_node *parse_declaration(bool is_file_scope);
static struct ast_node *parse_parameter_declaration(void);
static struct ast_node *parse_statement(void);
static struct ast_node *parse_block(void);

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
            case TOKEN_VOID:
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

static bool is_declaration_specifier(enum token_type type)
{
    switch (type) {
        case TOKEN_VOID:
        case TOKEN_INT:
        case TOKEN_STATIC:
        case TOKEN_EXTERN:
            return true;
        default:
            return false;
    }
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

    if (!right) return NULL;
    return AST_NEW(AST_BINARY, op, .binary.left = left, .binary.right = right);
}

static struct ast_node *assignment(struct ast_node *left)
{
    struct token op = parser_state.previous;

    struct ast_node *right = parse_expression(PREC_ASSIGNMENT);
    if (!right) return NULL;

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

static struct ast_node *call(struct ast_node *left)
{
    struct token call_tok = parser_state.previous;
    struct ast_node *args_head = NULL;
    struct ast_node *args_tail = NULL;

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            struct ast_node *arg = parse_expression(PREC_ASSIGNMENT);
            if (!arg)
                return NULL;
            LIST_APPEND(args_head, args_tail, arg);
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expected ')' after arguments");
    return AST_NEW(AST_CALL, call_tok,
            .call.calle = left,
            .call.args = args_head
    );
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
    [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
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
        consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'switch' condition");

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
        if (is_declaration_specifier(parser_state.current.type)) {
            for_init = parse_declaration(false);

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

    /*
     * If didn't match with any statement
     * It's either an expression-statement or goto label
     */
    struct ast_node *expr = parse_expression(PREC_ASSIGNMENT);
    if (!expr)
        return NULL;

    // Label
    if (expr->type == AST_IDENTIFIER && match(TOKEN_COLON)) {
        struct ast_node *stmt = parse_statement();
        return AST_NEW(AST_LABEL_STMT, expr->token,
                .label_stmt.stmt = stmt,
                .label_stmt.name = expr->token
        );
    }

    // Othwerwise expression statement
    consume(TOKEN_SEMICOLON, "Expected ';' after expression statement");
    return AST_NEW(AST_EXPR_STMT, parser_state.previous, .expr_stmt.expr = expr);
}

static struct ast_node *parse_block(void)
{
    struct ast_node *block = AST_NEW(AST_BLOCK, parser_state.previous);
    struct ast_node *tail = NULL;

    /*
     * block-item:
     *  declaration
     *  statement
     */
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        struct ast_node *item = NULL;
        if (is_declaration_specifier(parser_state.current.type))
            item = parse_declaration(false);
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

static void set_storage_class(struct decl_specs *specs,
                              enum storage_class storage,
                              struct token *tok)
{
     // One storage class may be given in declaration
     if (specs->storage) {
         error(tok, "Multiple storage class identifiers");
         return;
     }
     
     specs->storage = storage;
}

static struct decl_specs parse_declaration_specifiers(void)
{
    /*
     * Either storage-class-specifier
     * or type-specifier
     */
    struct decl_specs specs = {
        .storage = SC_NONE,
        .base_type = NULL
    };

    while (is_declaration_specifier(parser_state.current.type)) {
        if (match(TOKEN_EXTERN)) {
            set_storage_class(&specs, SC_EXTERN, &parser_state.previous);
        }
        else if (match(TOKEN_STATIC)) {
            set_storage_class(&specs, SC_STATIC, &parser_state.previous);
        } else if (match(TOKEN_VOID)) {
            if (specs.base_type)
                error(&parser_state.previous, "Duplicate type specifier");

            specs.base_type = type_void();
        } else if (match(TOKEN_INT)) {
            if (specs.base_type)
                error(&parser_state.previous, "Duplicate type specifier");

            specs.base_type = type_int();
        }
    }

    if (!specs.base_type)
        error(&parser_state.previous, "Type specifier missing");

    return specs;
}

static struct param_list parse_paremeter_list_or_empty(void)
{
    struct param_list list = {0};

    /*
     * C23 mode:
     *  int f() is treated as int f(void)
     */
    if (match(TOKEN_RIGHT_PAREN))
        return list;

    if (match(TOKEN_VOID)) {
        if (!check(TOKEN_RIGHT_PAREN))
            error(&parser_state.previous, "'void' be the first and only parameter");

        return list;
    }

    do {
        struct ast_node *param = parse_parameter_declaration();

        if (!param)
            break;

        LIST_APPEND(list.head, list.tail, param);

        list.types = realloc(list.types, sizeof(*list.types) * (list.count +1));
        list.types[list.count++] = param->var_decl.type;
    } while(match(TOKEN_COMMA));

    return list;
}

static struct parsed_declarator parse_declarator(struct type *base_type)
{
    /*
     * This now supports:
     *  int x
     *  int *p
     *  int f()
     *  int f(void)
     *  int f(int x, int y)
     *  int *f(void)
     */
    while (match(TOKEN_STAR))
        base_type = type_pointer(base_type);

    consume(TOKEN_IDENTIFIER, "Expected identifier");

    struct parsed_declarator decl = {
        .name = parser_state.previous,
        .type = base_type,
        .params = NULL
    };

    if (match(TOKEN_LEFT_PAREN)) {
        struct param_list params = parse_paremeter_list_or_empty();

        consume(TOKEN_RIGHT_PAREN, "Expected ')' after parameters list");

        decl.type = type_function(
                decl.type,
                params.types,
                params.count,
                params.is_variadic
        );

        decl.params = params.head;
    }

    return decl;
}

static struct ast_node *parse_parameter_declaration(void)
{
    // declaration-specifier declarator

    struct decl_specs specs = parse_declaration_specifiers();
    struct parsed_declarator decl = parse_declarator(specs.base_type);

    return AST_NEW(AST_VAR_DECL, decl.name,
            .var_decl.name = decl.name,
            .var_decl.type = decl.type,
            .var_decl.storage = specs.storage,
            .var_decl.init = NULL,
            .var_decl.is_parameter = true,
            .var_decl.is_definition = true,
            .var_decl.is_tentative = false,
            .var_decl.symbol = NULL
    );
}

static struct ast_node *make_declaration_node(struct decl_specs specs,
        struct parsed_declarator decl, struct ast_node *init, bool is_file_scope)
{
    if (decl.type->kind == TY_FUNCTION) {
        if (init)
            error(&decl.name, "Function declaration cannot have initializer");

        return AST_NEW(AST_FUN_DECL, decl.name,
                .fun_decl.name = decl.name,
                .fun_decl.type = decl.type,
                .fun_decl.storage = specs.storage,
                .fun_decl.params = decl.params,
                .fun_decl.body = NULL,
                .fun_decl.is_definition = false,
                .fun_decl.symbol = NULL
        );
    }

    bool is_tentative = false;
    bool is_definition = true;

    if (is_file_scope && init == NULL) {
        if (specs.storage == SC_EXTERN) {
            is_definition = false;
            is_tentative = false;
        } else {
            is_definition = false;
            is_tentative = true;
        }
    }

    return AST_NEW(AST_VAR_DECL, decl.name,
            .var_decl.name = decl.name,
            .var_decl.type = decl.type,
            .var_decl.storage = specs.storage,
            .var_decl.init = init,
            .var_decl.is_definition = is_definition,
            .var_decl.is_tentative = is_tentative,
            .var_decl.symbol = NULL
    );
}

static struct ast_node *parse_declaration(bool is_file_scope)
{
    // declaration-specifiers init-declarator-listopt;
    struct decl_specs specs = parse_declaration_specifiers();
    struct parsed_declarator decl = parse_declarator(specs.base_type);

    struct ast_node *init = NULL;
    if (match(TOKEN_EQUAL)) {
        if (decl.type->kind == TY_FUNCTION)
            error(&decl.name, "Function declaration cannot have initializer");

        init = parse_expression(PREC_ASSIGNMENT);
    }

    consume(TOKEN_SEMICOLON, "Expected ';' after declaration");

    return make_declaration_node(specs, decl, init, is_file_scope);
}

static struct ast_node *parse_function_definition(struct decl_specs specs,
        struct parsed_declarator decl)
{
    if (decl.type->kind != TY_FUNCTION) {
        error(&decl.name, "Function definition requires function declarator");
        return NULL;
    }

    if (specs.storage != SC_NONE &&
        specs.storage != SC_EXTERN &&
        specs.storage != SC_STATIC) {
        error(&decl.name, "Invalid storage class for function definition");
    }

    consume(TOKEN_LEFT_BRACE, "Expected function body");
    struct ast_node *body = parse_block();

    return AST_NEW(AST_FUN_DECL, decl.name,
        .fun_decl.name = decl.name,
        .fun_decl.type = decl.type,
        .fun_decl.storage = specs.storage,
        .fun_decl.params = decl.params,
        .fun_decl.body = body,
        .fun_decl.is_definition = true,
        .fun_decl.symbol = NULL
    );
}

static struct ast_node *parse_external_declaration(void)
{
    /*
     * external-declaration:
     *  function-definition
     *  declaration
     *
     *  We parse declaration-specifiers + declarator first, the choose between
     *  a function definition or a declaration
     */
    struct decl_specs specs = parse_declaration_specifiers();
    struct parsed_declarator decl = parse_declarator(specs.base_type);

    if (decl.type->kind == TY_FUNCTION && check(TOKEN_LEFT_BRACE))
        return parse_function_definition(specs, decl);

    struct ast_node *init = NULL;

    if (match(TOKEN_EQUAL)) {
        if(decl.type->kind == TY_FUNCTION)
            error(&decl.name, "Function declarator cannot have initializer");

        init = parse_expression(PREC_ASSIGNMENT);
    }

    consume(TOKEN_SEMICOLON, "Expected ';' after external declaration");

    return make_declaration_node(specs, decl, init, true);
}

struct ast_node *parse_translation_unit(const char *source)
{
    parser_state = (struct parser){0};
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
