#include <stdlib.h>

#include "x86.h"
#include "ir.h"

/* Phase 1: Convert TACKY IR to ASM AST. Keeps temporary variables (pseduos) */

static operator_e convert_op(ir_op_e op)
{
    return (op == OP_NEGATE ? ASM_OP_NEG : ASM_OP_NOT);
}

static operand_t convert_val(ir_val_t v)
{
    operand_t op = {0};

    if (v.kind == IR_VAL_CONSTANT) {
        op.type = OPERAND_IMM;
        op.imm = v.constant;
    } else {
        op.type = OPERAND_PSEUDO;
        op.pseudo = v.var_id;
    }

    return op;
}

static void append_instr(asm_function_t *fn, asm_instr_t *instr)
{
    if (!fn->last) fn->first = instr;
    else fn->last->next = instr;
    fn->last = instr;
}

static void emit_instr(asm_function_t *fn, ir_instr_t *instr)
{
    switch (instr->type) {
        case IR_RETURN: {
            asm_instr_t *mov = calloc(1, sizeof(asm_instr_t));
            mov->type = ASM_MOV;
            mov->mov.src.imm = instr->ret.src.constant;
            mov->mov.dst.reg = REG_AX;
            append_instr(fn, mov);

            asm_instr_t *ret = calloc(1, sizeof(asm_instr_t));
            ret->type = ASM_RET;
            append_instr(fn, ret);

            break;
        }
        case IR_UNARY: {
            operand_t src = convert_val(instr->unary.src);
            operand_t dst = convert_val(instr->unary.dst);

            asm_instr_t *mov = calloc(1, sizeof(asm_instr_t));
            mov->type = ASM_MOV;
            mov->mov.src = src;
            mov->mov.dst = dst;
            append_instr(fn, mov);

            asm_instr_t *unary = calloc(1, sizeof(asm_instr_t));
            unary->type = ASM_UNARY;
            unary->unary.op = convert_op(instr->unary.op);
            unary->unary.dst = dst;
            append_instr(fn, unary);

            break;
        }
    }
}

static asm_function_t *emit_function(ir_function_t *fn)
{
    asm_function_t *asm_fn = calloc(1, sizeof(asm_function_t));
    asm_fn->name = fn->name;
    asm_fn->name_length = fn->name_length;

    ir_instr_t *instr = fn->first;
    while (instr) {
        emit_instr(asm_fn, instr);
        instr = instr->next;
    }
    return asm_fn;
}

static asm_program_t *asm_phase1(ir_program_t *ir)
{
    asm_program_t *program = calloc(1, sizeof(asm_program_t));
    program->function = emit_function(ir->function);
    return program;
}

/* Phase 2: Replace pseduo operands with stack operands */

static void convert_pseudo(operand_t *op, int *map, int *next_offset)
{
    if (op->type != OPERAND_PSEUDO)
        return;
    
    int id = op->pseudo;

    if (map[id] == 0) {
        *next_offset += 4;
        map[id] = -*next_offset;
    }

    op->type = OPERAND_STACK;
    op->stack = map[id];
}

static int asm_phase2(asm_program_t *program)
{
    int pseudo_map[64] = {0};
    int next_offset = 0;

    asm_function_t *function = program->function;
    asm_instr_t *instr = function->first;
    while(instr) {
        switch (instr->type) {
            case ASM_MOV: {
                convert_pseudo(&instr->mov.src, pseudo_map, &next_offset);
                convert_pseudo(&instr->mov.dst, pseudo_map, &next_offset);
            }
            case ASM_UNARY: {
                convert_pseudo(&instr->unary.dst, pseudo_map, &next_offset);
            }
            default:
                break;
        }

        instr = instr->next;
    }

    return next_offset;
}

/* Phase 3: Insert allocate_stack, fix AST */

static void asm_phase3(asm_program_t *program, int stack_offset)
{
    asm_function_t *function = program->function;

    // Insert allocate_stack at the beginning of function
    asm_instr_t *instr = calloc(1, sizeof(asm_instr_t));
    instr->type = ASM_ALLOCSTACK;
    instr->allocate_stack.val = stack_offset;
    instr->next = function->first;
    function->first = instr;

    asm_instr_t *curr = function->first;
    asm_instr_t *prev = NULL;
    while (curr) {
        switch (curr->type) {
            case ASM_MOV: {
                if (curr->mov.src.type == OPERAND_STACK &&
                    curr->mov.dst.type == OPERAND_STACK) {
                    operand_t src = curr->mov.src;
                    operand_t dst = curr->mov.dst;
                    operand_t scratch = { .type = OPERAND_REG, .reg = REG_R10 };

                    asm_instr_t *mov1 = calloc(1, sizeof(asm_instr_t));
                    mov1->type = ASM_MOV;
                    mov1->mov.src = src;
                    mov1->mov.dst = scratch;

                    asm_instr_t *mov2 = calloc(1, sizeof(asm_instr_t));
                    mov2->type = ASM_MOV;
                    mov2->mov.src = scratch;
                    mov2->mov.dst = dst;
                    
                    // Splice into list
                    if (prev)
                        prev->next = mov1;
                    else
                        function->first = mov1;

                    mov1->next = mov2;
                    mov2->next = curr->next;

                    if (function->last == curr)
                        function->last = mov2;

                    asm_instr_t *old = curr;
                    curr = mov2;
                    free(old);
                }
            }
            default:
                break;
        }
        curr = curr->next;
        prev = curr;
    }
}

void emit_x86(ir_program_t *ir)
{
    asm_program_t *program = asm_phase1(ir);
    int stack_offset = asm_phase2(program);
    asm_phase3(program, stack_offset);
}
