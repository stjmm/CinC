#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "x86.h"
#include "ir.h"

#define EMIT_OPERAND(file, op)      \
    do {                            \
        operand_t _o = (op);        \
        if (_o.type == OPERAND_REG) \
            fprintf((file), "%%%s", reg_name_32(_o.reg)); \
        else if (_o.type == OPERAND_IMM) \
            fprintf((file), "$%d", _o.imm); \
        else \
            fprintf((file), "%d(%%rbp)", _o.stack); \
    } while(0)

/* Phase 1: Convert TACKY IR to ASM AST. Keeps temporary variables (pseduos) */

static operator_e convert_unop(ir_unary_op_e op)
{
    return (op == IR_OP_NEGATE ? ASM_OP_NEG : ASM_OP_NOT);
}

static operator_e convert_binop(ir_binary_op_e op)
{
    operator_e operator;
    if (op == IR_OP_ADD) operator = ASM_OP_ADD;
    if (op == IR_OP_SUBTRACT) operator = ASM_OP_SUB;
    if (op == IR_OP_MULTIPLY) operator = ASM_OP_MULT;
    return operator;
}

const char* reg_name_32(asm_reg_e r) {
    switch (r) {
        case REG_AX:  return "eax";
        case REG_DX:  return "edx";
        case REG_R10: return "r10d";
        case REG_R11: return "r11d";
        default:      return "unknown";
    }
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

static void chain(asm_instr_t *a, asm_instr_t *b) { a->next = b; }

static operand_t reg_operand(asm_reg_e r)
{
    return (operand_t){ .type = OPERAND_REG, .reg = r, };
}

static asm_instr_t *make_mov(operand_t src, operand_t dst)
{
    asm_instr_t *instr = calloc(1, sizeof(asm_instr_t));
    instr->type = ASM_MOV;
    instr->mov.src = src;
    instr->mov.dst = dst;
    return instr;
}

static void emit_instr(asm_function_t *fn, ir_instr_t *instr)
{
    switch (instr->type) {
        case IR_RETURN: {
            operand_t src = convert_val(instr->ret.src);
            asm_instr_t *mov = calloc(1, sizeof(asm_instr_t));
            mov->type = ASM_MOV;
            mov->mov.src = src;
            mov->mov.dst = reg_operand(REG_AX);
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
            unary->unary.op = convert_unop(instr->unary.op);
            unary->unary.dst = dst;
            append_instr(fn, unary);

            break;
        }
        case IR_BINARY: {
            operand_t src1 = convert_val(instr->binary.src1);
            operand_t src2 = convert_val(instr->binary.src2);
            operand_t dst = convert_val(instr->binary.dst);

            // Special case for division and remainder
            // Converts Binary to Mov(src, AX), Cdq, Idiv(src), Mov(AX/DX, dst)
            if (instr->binary.op == IR_OP_DIVIDE || instr->binary.op == IR_OP_REMAINDER) {
                asm_instr_t *mov1 = calloc(1, sizeof(asm_instr_t));
                mov1->type = ASM_MOV;
                mov1->mov.src = src1;
                mov1->mov.dst = reg_operand(REG_AX);
                append_instr(fn, mov1);

                asm_instr_t *cdq = calloc(1, sizeof(asm_instr_t));
                cdq->type = ASM_CDQ;
                append_instr(fn, cdq);

                asm_instr_t *idiv = calloc(1, sizeof(asm_instr_t));
                idiv->type = ASM_IDIV;
                idiv->idiv.operand = src2;
                append_instr(fn, idiv);

                asm_instr_t *mov2 = calloc(1, sizeof(asm_instr_t));
                mov2->type = ASM_MOV;
                mov2->mov.src.reg = instr->binary.op == IR_OP_DIVIDE ? REG_AX : REG_DX;
                mov2->mov.dst = dst;
                append_instr(fn, mov2);
                break;
            }

            // Converts IR Binary(op, src1, src2, dst)
            // into ASM Mov(src1, dst) and Binary(op, src2, dst)
            asm_instr_t *mov = calloc(1, sizeof(asm_instr_t));
            mov->type = ASM_MOV;
            mov->mov.src = src1;
            mov->mov.dst = dst;
            append_instr(fn, mov);

            asm_instr_t *binary = calloc(1, sizeof(asm_instr_t));
            binary->type = ASM_BINARY;
            binary->binary.op = convert_binop(instr->binary.op);
            binary->binary.src = src2;
            binary->binary.dst = dst;
            append_instr(fn, binary);
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
            case ASM_MOV:
                convert_pseudo(&instr->mov.src, pseudo_map, &next_offset);
                convert_pseudo(&instr->mov.dst, pseudo_map, &next_offset);
                break;
            case ASM_UNARY:
                convert_pseudo(&instr->unary.dst, pseudo_map, &next_offset);
                break;
            case ASM_BINARY:
                convert_pseudo(&instr->binary.src, pseudo_map, &next_offset);
                convert_pseudo(&instr->binary.dst, pseudo_map, &next_offset);
                break;
            default:
                break;
        }

        instr = instr->next;
    }

    return next_offset;
}

static asm_instr_t *replace_instr(asm_function_t *fn, asm_instr_t *prev,
                                  asm_instr_t *curr, asm_instr_t *first_new,
                                  asm_instr_t *last_new)
{
    last_new->next = curr->next;

    if (prev) prev->next = first_new;
    else fn->first = first_new;

    if (fn->last == curr) fn->last = last_new;

    free(curr);
    return last_new;
}

/* Phase 3: Insert allocate_stack, fix memory-memory operations */
static void asm_phase3(asm_program_t *program, int stack_offset)
{
    asm_function_t *fn = program->function;

    // Insert allocate_stack at the beginning of function
    asm_instr_t *alloc = calloc(1, sizeof(asm_instr_t));
    alloc->type = ASM_ALLOCSTACK;
    alloc->allocate_stack.val = stack_offset;
    alloc->next = fn->first;
    fn->first = alloc;

    asm_instr_t *prev = NULL;
    asm_instr_t *curr = fn->first;
    
    operand_t r10 = reg_operand(REG_R10);
    operand_t r11 = reg_operand(REG_R11);

    while (curr) {
        switch (curr->type) {
            
            // movl mem1, mem2 -> movl mem1, %r10d / movl %r10d, mem2
            case ASM_MOV: {
                if (curr->mov.src.type != OPERAND_STACK &&
                    curr->mov.dst.type != OPERAND_STACK) break;

                asm_instr_t *m1 = make_mov(curr->mov.src, r10);
                asm_instr_t *m2 = make_mov(r10, curr->mov.dst);
                chain(m1, m2);
                curr = replace_instr(fn, prev, curr, m1, m2);
                break;
            }

            // addl/subl mem1, mem2 -> movl mem1, %r10d / movl %r10d, mem2
            case ASM_BINARY: {
                bool src_mem = curr->binary.src.type == OPERAND_STACK;
                bool dst_mem = curr->binary.dst.type == OPERAND_STACK;
                bool is_mul = curr->binary.op == ASM_OP_MULT;

                if (is_mul && dst_mem) {
                    // imull src, mem  ->  movl mem, %r11d / imull src, %r11d / movl %r11d, mem
                    asm_instr_t *m1 = make_mov(curr->binary.dst, r11);
                    asm_instr_t *op = calloc(1, sizeof(asm_instr_t));
                    op->type       = ASM_BINARY;
                    op->binary.op  = ASM_OP_MULT;
                    op->binary.src = curr->binary.src;
                    op->binary.dst = r11;
                    asm_instr_t *m2 = make_mov(r11, curr->binary.dst);
                    chain(m1, op); chain(op, m2);
                    curr = replace_instr(fn, prev, curr, m1, m2);
                } else if (!is_mul && src_mem && dst_mem) {
                    // addl/subl mem, mem  ->  movl src, %r10d / op %r10d, dst
                    asm_instr_t *m1 = make_mov(curr->binary.src, r10);
                    asm_instr_t *op = calloc(1, sizeof(asm_instr_t));
                    op->type       = ASM_BINARY;
                    op->binary.op  = curr->binary.op;
                    op->binary.src = r10;
                    op->binary.dst = curr->binary.dst;
                    chain(m1, op);
                    curr = replace_instr(fn, prev, curr, m1, op);
                }
                break;
            }
            
            // idiv $imm -> movl $imm, %r10d / idiv %r10d
            case ASM_IDIV: {
                if (curr->idiv.operand.type != OPERAND_IMM) break;

                asm_instr_t *m1 = make_mov(curr->idiv.operand, r10);
                asm_instr_t *idiv = calloc(1, sizeof(asm_instr_t));
                idiv->type = ASM_IDIV;
                idiv->idiv.operand = r10;
                chain(m1, idiv);
                replace_instr(fn, prev, curr, idiv, m1);
            }
            default:
                break;
        }
        prev = curr;
        curr = curr->next;
    }
}

void emit_x86(ir_program_t *ir, FILE *file)
{
    asm_program_t *program = asm_phase1(ir);
    int stack_offset = asm_phase2(program);
    asm_phase3(program, stack_offset);

    asm_function_t *function = program->function;
    asm_instr_t *instr = function->first;

    fprintf(file, "    .globl %.*s\n", function->name_length, function->name);
    fprintf(file, "%.*s:\n", function->name_length, function->name);
    fprintf(file, "    pushq    %%rbp\n");
    fprintf(file, "    movq     %%rsp, %%rbp\n");

    while (instr) {
        switch (instr->type) {
            case ASM_ALLOCSTACK: {
                fprintf(file, "    subq     $%d, %%rsp\n", instr->allocate_stack.val);
                break;
            }
            case ASM_CDQ: {
                fprintf(file, "    cdq\n");
                break;
            }
            case ASM_MOV: {
                operand_t src = instr->mov.src;
                operand_t dst = instr->mov.dst;

                fprintf(file, "    movl     ");
                EMIT_OPERAND(file, src);
                fprintf(file, ", ");
                EMIT_OPERAND(file, dst);
                fprintf(file, "\n");
                break;
            }
            case ASM_UNARY: {
                const char *op = NULL;
                switch (instr->unary.op) {
                    case ASM_OP_NEG: op = "negl"; break;
                    case ASM_OP_NOT: op = "notl"; break;
                    default:         op = "???";  break;
                }
                fprintf(file, "    %s     ", op);
                EMIT_OPERAND(file, instr->unary.dst);
                fprintf(file, "\n");
                break;
            }
            case ASM_BINARY: {
                const char *op = NULL;
                switch(instr->binary.op) {
                    case ASM_OP_ADD: op = "addl"; break;
                    case ASM_OP_SUB: op = "subl"; break;
                    case ASM_OP_MULT: op = "imull"; break;
                    default: op = "???"; break;
                }
                fprintf(file, "    %s     ", op);
                EMIT_OPERAND(file, instr->binary.src);
                fprintf(file, ", ");
                EMIT_OPERAND(file, instr->binary.dst);
                fprintf(file, "\n");
                break;
            }
            case ASM_IDIV: {
                fprintf(file, "    idivl    ");
                EMIT_OPERAND(file, instr->idiv.operand);
                fprintf(file, "\n");
                break;
            }
            case ASM_RET: {
                fprintf(file, "    movq     %%rbp, %%rsp\n");
                fprintf(file, "    popq     %%rbp\n");
                fprintf(file, "    ret\n");
                break;
            }
        }
        instr = instr->next;
    }
    
    fprintf(file, "\n    .section .note.GNU-stack,\"\",@progbits\n");
}
