#include <stdlib.h>
#include <stdio.h>

#include "ir.h"
#include "lexer.h"

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
        case TOKEN_GREATER:       return IR_GREATER;
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

static void emit_copy(struct ir_function *fn, struct ir_val src, struct ir_val dst)
{
    struct ir_instr *i = calloc(1, sizeof(struct ir_instr));
    *i = (struct ir_instr){ .type = IR_COPY, .copy.src = src, .copy.dst = dst };
    append_instr(fn, i);
}

static void emit_jump(struct ir_function *fn, int label_id)
{
    struct ir_instr *i = calloc(1, sizeof(struct ir_instr));
    *i = (struct ir_instr){ .type = IR_JUMP, .jump.label_id = label_id };
    append_instr(fn, i);
}

static void emit_jump_cond(struct ir_function *fn, struct ir_val cond,
        int label_id, enum ir_instr_type type)
{
    struct ir_instr *i = calloc(1, sizeof(struct ir_instr));
    if (type == IR_JUMP_IF_ZERO) {
        *i = (struct ir_instr){ .type = type, .jump_if_zero.cond = cond, .jump_if_zero.label_id = label_id};
    } else {
        *i = (struct ir_instr){ .type = type, .jump_if_not_zero.cond = cond, .jump_if_not_zero.label_id = label_id};
    }
    append_instr(fn, i);
}

static void emit_label(struct ir_function *fn, int label_id)
{
    struct ir_instr *i = calloc(1, sizeof(struct ir_instr));
    i->type = IR_LABEL;
    i->label.label_id = label_id;
    append_instr(fn, i);
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
            if (expr->token.type == TOKEN_AND_AND) {
                int false_label = make_label();
                int end_label = make_label();
                struct ir_val dst = make_temp();

                struct ir_val v1 = emit_expr(expr->binary.left, fn);
                emit_jump_cond(fn, v1, false_label, IR_JUMP_IF_ZERO);

                struct ir_val v2 = emit_expr(expr->binary.right, fn);
                emit_jump_cond(fn, v2, false_label, IR_JUMP_IF_ZERO);

                emit_copy(fn, make_constant(1), dst);
                emit_jump(fn, end_label);

                emit_label(fn, false_label);

                emit_copy(fn, make_constant(0), dst);

                emit_label(fn, end_label);

                return dst;
            } else if (expr->token.type == TOKEN_OR_OR) {
                int true_label = make_label();
                int end_label = make_label();
                struct ir_val dst = make_temp();

                struct ir_val v1 = emit_expr(expr->binary.left, fn);
                emit_jump_cond(fn, v1, true_label, IR_JUMP_IF_NOT_ZERO);

                struct ir_val v2 = emit_expr(expr->binary.right, fn);
                emit_jump_cond(fn, v2, true_label, IR_JUMP_IF_NOT_ZERO);

                emit_copy(fn, make_constant(0), dst);
                emit_jump(fn, end_label);

                emit_label(fn, true_label);

                emit_copy(fn, make_constant(1), dst);

                emit_label(fn, end_label);

                return dst;
            }

            // Standard case for binar operations
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
