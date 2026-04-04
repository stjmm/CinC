#include <stdlib.h>
#include <stdio.h>

#include "ir.h"

static struct ir_val make_constant(long c) 
{
    return (struct ir_val){ .type = IR_VAL_CONSTANT, .constant = c };
}

static struct ir_val make_temp(void)
{
    static int id = 0;
    return (struct ir_val){ .type = IR_VAL_VAR, .var_id = id++ };
}

static int make_label(void)
{
    static int label_id = 0;
    return label_id++;
}

static enum ir_unary_op convert_unop(struct token tok)
{
    switch (tok.type) {
        case TOKEN_MINUS: return IR_NEGATE;
        case TOKEN_TILDE: return IR_COMPLEMENT;
        case TOKEN_BANG:  return IR_NOT;
        default: fprintf(stderr, "unknown unop\n"); exit(1);
    }
}

static enum ir_binary_op convert_binop(struct token tok)
{
    switch (tok.type) {
        case TOKEN_PLUS:          return IR_ADD;
        case TOKEN_MINUS:         return IR_SUBTRACT;
        case TOKEN_STAR:          return IR_MULTIPLY;
        case TOKEN_SLASH:         return IR_DIVIDE;
        case TOKEN_PERCENT:       return IR_REMAINDER;
        case TOKEN_EQUAL_EQUAL:   return IR_EQUAL;
        case TOKEN_BANG_EQUAL:    return IR_NOT_EQUAL;
        case TOKEN_LESS:          return IR_LESS;
        case TOKEN_LESS_EQUAL:    return IR_LESS_EQUAL;
        case TOKEN_GREATER:       return IR_LESS_EQUAL;
        case TOKEN_GREATER_EQUAL: return IR_GREATER_EQUAL;
        default: fprintf(stderr, "unknown binop\n"); exit(1);
    }
}

static void append_instr(struct ir_function *fn, struct ir_instr *instr)
{
    if (!fn->first) fn->first = instr;
    else fn->last->next = instr;
    fn->last = instr;
}


static struct ir_val emit_expr(struct ast_node *expr, struct ir_function *fn)
{
    switch (expr->type) {
        case AST_CONSTANT:
            return make_constant(expr->constant.value);
        case AST_UNARY: {
            struct ir_val src = emit_expr(expr->unary.expr, fn);
            struct ir_val dst = make_temp();

            struct ir_instr *instr = calloc(1, sizeof(struct ir_instr));
            *instr = (struct ir_instr){
                .type = IR_UNARY,
                .unary.op = convert_unop(expr->token),
                .unary.src = src,
                .unary.dst = dst,
            };
            append_instr(fn, instr);
            return dst;
        }
        case AST_BINARY: {
            struct ir_val v1 = emit_expr(expr->binary.left, fn);
            struct ir_val v2 = emit_expr(expr->binary.right, fn);
            struct ir_val dst = make_temp();

            struct ir_instr *instr = calloc(1, sizeof(struct ir_instr));
            *instr = (struct ir_instr){
                .type = IR_BINARY,
                .binary.op = convert_binop(expr->token),
                .binary.src1 = v1,
                .binary.src2 = v2,
                .binary.dst = dst,
            };
            append_instr(fn, instr);
            return dst;
        }
        default:
            fprintf(stderr, "tacky_emit: unhandled expr kind\n");
            exit(1);
    }
}

static void emit_stmt(struct ast_node *stmt, struct ir_function *fn)
{
    switch (stmt->type) {
        case AST_RETURN: {
            struct ir_val src = emit_expr(stmt->return_stmt.expr, fn);
            struct ir_instr *instr = calloc(1, sizeof(struct ir_instr));
            *instr = (struct ir_instr){
                .type = IR_RETURN,
                .ret.src = src
            };
            append_instr(fn, instr);
            break;
        }
        default:
            fprintf(stderr, "unhandled stmt kind\n");
            exit(1);
    }
}

static struct ir_function *emit_function(struct ast_node *fn_node)
{
    struct ir_function *fn = calloc(1, sizeof(struct ir_function));
    fn->name = fn_node->token.start;
    fn->name_length = fn_node->token.length;

    struct ast_node *stmt = fn_node->function.body->block.first;
    while (stmt) {
        emit_stmt(stmt, fn);
        stmt = stmt->next;
    }
    return fn;
}

struct ir_program *tacky_build(struct ast_node *root)
{
    struct ir_program *program = calloc(1, sizeof(struct ir_program));
    program->function = emit_function(root->program.first);
    return program;
}
