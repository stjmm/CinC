#include <stdlib.h>
#include <stdio.h>

#include "ir.h"

static ir_val_t make_constant(long c) 
{
    return (ir_val_t){ .kind = IR_VAL_CONSTANT, .constant = c };
}

static ir_val_t make_temp(void)
{
    static int id = 0;
    return (ir_val_t){ .kind = IR_VAL_VAR, .var_id = id++ };
}

static ir_op_e convert_unop(token_t tok)
{
    switch (tok.type) {
        case TOKEN_MINUS: return OP_NEGATE;
        case TOKEN_TILDE: return OP_COMPLEMENT;
        default: fprintf(stderr, "unknown op\n"); exit(1);
    }
}

static void append_instr(ir_function_t *fn, ir_instr_t *instr)
{
    if (!fn->first) fn->first = instr;
    else fn->last->next = instr;
    fn->last = instr;
}


static ir_val_t emit_expr(ast_node_t *expr, ir_function_t *fn)
{
    switch (expr->kind) {
        case AST_CONSTANT:
            return make_constant(expr->constant.value);
        case AST_UNARY: {
            ir_val_t src = emit_expr(expr->unary.expr, fn);
            ir_val_t dst = make_temp();

            ir_instr_t *instr = calloc(1, sizeof(ir_instr_t));
            *instr = (ir_instr_t){
                .type = IR_UNARY,
                .unary.op = convert_unop(expr->token),
                .unary.src = src,
                .unary.dst = dst,
            };
            append_instr(fn, instr);
            return dst;
        }
        default:
            fprintf(stderr, "tacky_emit: unhandled expr kind\n");
            exit(1);
    }
}

static void emit_stmt(ast_node_t *stmt, ir_function_t *fn)
{
    switch (stmt->kind) {
        case AST_RETURN: {
            ir_val_t src = emit_expr(stmt->return_stmt.expr, fn);
            ir_instr_t *instr = calloc(1, sizeof(ir_instr_t));
            *instr = (ir_instr_t){
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

static ir_function_t *emit_function(ast_node_t *fn_node)
{
    ir_function_t *fn = calloc(1, sizeof(ir_function_t));
    fn->name = fn_node->token.start;
    fn->name_length = fn_node->token.length;

    ast_node_t *stmt = fn_node->function.body->block.first;
    while (stmt) {
        emit_stmt(stmt, fn);
        stmt = stmt->next;
    }
    return fn;
}

ir_program_t *tacky_build(ast_node_t *root)
{
    ir_program_t *program = calloc(1, sizeof(ir_program_t));
    program->function = emit_function(root->program.first);
    return program;
}
