#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "x86.h"
#include "ir.h"

#define EMIT_OPERAND(file, op)      \
    do {                            \
        struct operand _o = (op);        \
        if (_o.type == OPERAND_REG) \
            fprintf((file), "%%%s", reg_name_32(_o.reg)); \
        else if (_o.type == OPERAND_IMM) \
            fprintf((file), "$%d", _o.imm); \
        else \
            fprintf((file), "%d(%%rbp)", _o.stack); \
    } while(0)

#define EMIT_OPERAND_8(file, op)      \
    do {                            \
        struct operand _o = (op);        \
        if (_o.type == OPERAND_REG) \
            fprintf((file), "%%%s", reg_name_8(_o.reg)); \
        else if (_o.type == OPERAND_IMM) \
            fprintf((file), "$%d", _o.imm); \
        else \
            fprintf((file), "%d(%%rbp)", _o.stack); \
    } while(0)

/* Phase 1: Convert TACKY IR to ASM AST. Keeps temporary variables (pseduos) */

static enum asm_op convert_unop(enum ir_unary_op op)
{
    enum asm_op operator;
    if (op == IR_NEGATE)     operator = ASM_NEG;
    if (op == IR_COMPLEMENT) operator = ASM_NOT;
    return operator;
}

static enum asm_op convert_binop(enum ir_binary_op op)
{
    enum asm_op operator;
    if (op == IR_ADD)      operator = ASM_ADD;
    if (op == IR_SUBTRACT) operator = ASM_SUB;
    if (op == IR_MULTIPLY) operator = ASM_IMUL;
    return operator;
}

static enum cond_code convert_to_cond(enum ir_binary_op op)
{
    enum cond_code code;
    if (op == IR_EQUAL)         code = COND_E;
    if (op == IR_NOT_EQUAL)     code = COND_NE;
    if (op == IR_LESS_EQUAL)    code = COND_LE;
    if (op == IR_LESS)          code = COND_L;
    if (op == IR_GREATER)       code = COND_G;
    if (op == IR_GREATER_EQUAL) code = COND_GE;
    return code;
}

const char *reg_name_32(enum reg r)
{
    switch (r) {
        case REG_AX:  return "eax";
        case REG_DX:  return "edx";
        case REG_R10: return "r10d";
        case REG_R11: return "r11d";
        default:      return "unknown";
    }
}

const char *reg_name_8(enum reg r)
{
    switch (r) {
        case REG_AX:  return "al";
        case REG_DX:  return "dl";
        case REG_R10: return "r10b";
        case REG_R11: return "r11b";
        default:      return "unknown";
    }
}

const char *cond_suffix(enum cond_code c)
{
    switch (c) {
        case COND_E:  return "e";
        case COND_NE: return "ne";
        case COND_L:  return "l";
        case COND_LE: return "le";
        case COND_G:  return "g";
        case COND_GE: return "ge";
        default:      return "unknown";
    }
}

static struct operand convert_val(struct ir_val v)
{
    struct operand op = {0};

    if (v.type == IR_VAL_CONSTANT) {
        op.type = OPERAND_IMM;
        op.imm = v.constant;
    } else {
        op.type = OPERAND_PSEUDO;
        op.pseudo = v.var_id;
    }

    return op;
}

static struct operand make_reg_operand(enum reg r)
{
    return (struct operand){ .type = OPERAND_REG, .reg = r, };
}

static struct operand make_imm_operand(int imm)
{
    return (struct operand){ .type = OPERAND_IMM, .imm = imm };
}

static void append_instr(struct asm_function *fn, struct asm_instr *instr)
{
    if (!fn->last) fn->first = instr;
    else fn->last->next = instr;
    fn->last = instr;
}

static void chain_instr(struct asm_instr *a, struct asm_instr *b) { a->next = b; }

static struct asm_instr *make_mov(struct operand src, struct operand dst)
{
    struct asm_instr *instr = calloc(1, sizeof(struct asm_instr));
    instr->type = ASM_MOV;
    instr->mov.src = src;
    instr->mov.dst = dst;
    return instr;
}

static struct asm_instr *make_cmp(struct operand oper1, struct operand oper2)
{
    struct asm_instr *instr = calloc(1, sizeof(struct asm_instr));
    instr->type = ASM_CMP;
    instr->cmp.oper1 = oper1;
    instr->cmp.oper2 = oper2;
    return instr;
}

static void emit_instr(struct asm_function *fn, struct ir_instr *instr)
{
    switch (instr->type) {
        case IR_RETURN: {
            struct operand src = convert_val(instr->ret.src);
            struct asm_instr *mov = make_mov(src, make_reg_operand(REG_AX));
            append_instr(fn, mov);

            struct asm_instr *ret = calloc(1, sizeof(struct asm_instr));
            ret->type = ASM_RET;
            append_instr(fn, ret);
            break;
        }
        case IR_UNARY: {
            struct operand src = convert_val(instr->unary.src);
            struct operand dst = convert_val(instr->unary.dst);

            if (instr->unary.op == IR_NOT) {
                struct asm_instr *cmp = make_cmp(make_imm_operand(0), src);
                append_instr(fn, cmp);

                struct asm_instr *mov = make_mov(make_imm_operand(0), dst);
                append_instr(fn, mov);

                struct asm_instr *set_cc = calloc(1, sizeof(struct asm_instr));
                set_cc->type = ASM_SETCC;
                set_cc->set_cc.code = COND_E;
                set_cc->set_cc.oper = dst;
                append_instr(fn, set_cc);
                break;
            }

            struct asm_instr *mov = make_mov(src, dst);
            append_instr(fn, mov);

            struct asm_instr *unary = calloc(1, sizeof(struct asm_instr));
            unary->type = ASM_UNARY;
            unary->unary.op = convert_unop(instr->unary.op);
            unary->unary.dst = dst;
            append_instr(fn, unary);

            break;
        }
        case IR_BINARY: {
            struct operand src1 = convert_val(instr->binary.src1);
            struct operand src2 = convert_val(instr->binary.src2);
            struct operand dst = convert_val(instr->binary.dst);

            // Special case for division and remainder
            // Converts Binary to Mov(src, AX), Cdq, Idiv(src), Mov(AX/DX, dst)
            if (instr->binary.op == IR_DIVIDE || instr->binary.op == IR_REMAINDER) {
                struct asm_instr *mov1 = make_mov(src1, make_reg_operand(REG_AX));

                struct asm_instr *cdq = calloc(1, sizeof(struct asm_instr));
                cdq->type = ASM_CDQ;

                struct asm_instr *idiv = calloc(1, sizeof(struct asm_instr));
                idiv->type = ASM_IDIV;
                idiv->idiv.idiv_operand = src2;

                append_instr(fn, mov1);
                append_instr(fn, cdq);
                append_instr(fn, idiv);

                struct operand result_reg = make_reg_operand(instr->binary.op == IR_DIVIDE ? REG_AX : REG_DX);
                struct asm_instr *mov2 = make_mov(result_reg, dst);
                append_instr(fn, mov2);
                break;
            }

            if (instr->binary.op == IR_EQUAL || instr->binary.op == IR_NOT_EQUAL ||
                instr->binary.op == IR_LESS || instr->binary.op == IR_LESS_EQUAL ||
                instr->binary.op == IR_GREATER || instr->binary.op == IR_GREATER_EQUAL) {
                struct asm_instr *cmp = make_cmp(src2, src1);
                append_instr(fn, cmp);

                struct asm_instr *mov = make_mov(make_imm_operand(0), dst);
                append_instr(fn, mov);

                struct asm_instr *set_cc = calloc(1, sizeof(struct asm_instr));
                set_cc->type = ASM_SETCC;
                set_cc->set_cc.code = convert_to_cond(instr->binary.op);
                set_cc->set_cc.oper = dst;
                append_instr(fn, set_cc);
                break;
            }

            // Converts IR Binary(op, src1, src2, dst)
            // into ASM Mov(src1, dst) and Binary(op, src2, dst)
            struct asm_instr *mov = make_mov(src1, dst);
            append_instr(fn, mov);

            struct asm_instr *binary = calloc(1, sizeof(struct asm_instr));
            binary->type = ASM_BINARY;
            binary->binary.op = convert_binop(instr->binary.op);
            binary->binary.src = src2;
            binary->binary.dst = dst;
            append_instr(fn, binary);
            break;
        }
        case IR_JUMP_IF_ZERO: {
            struct operand val = convert_val(instr->jump_if_zero.cond);
            struct operand imm = make_imm_operand(0);
            struct asm_instr *cmp = make_cmp(val, imm);
            append_instr(fn, cmp);

            struct asm_instr *jmp_cc = calloc(1, sizeof(struct asm_instr));
            jmp_cc->type = ASM_JMPCC;
            jmp_cc->jmp_cc.code = COND_E;
            jmp_cc->jmp_cc.identifier = instr->jump_if_zero.label_id;
            append_instr(fn, jmp_cc);
            break;
        }
        case IR_JUMP_IF_NOT_ZERO: {
            struct operand val = convert_val(instr->jump_if_not_zero.cond);
            struct operand imm = make_imm_operand(0);
            struct asm_instr *cmp = make_cmp(val, imm);
            append_instr(fn, cmp);

            struct asm_instr *jmp_cc = calloc(1, sizeof(struct asm_instr));
            jmp_cc->type = ASM_JMPCC;
            jmp_cc->jmp_cc.code = COND_NE;
            jmp_cc->jmp_cc.identifier = instr->jump_if_not_zero.label_id;
            append_instr(fn, jmp_cc);
            break;
        }
        case IR_JUMP: {
            struct asm_instr *jump = calloc(1, sizeof(struct asm_instr));
            jump->type = ASM_JMP;
            jump->jmp.identifier = instr->jump.label_id;
            append_instr(fn, jump);
            break;
        }
        case IR_COPY: {
            struct operand src = convert_val(instr->copy.src);
            struct operand dst = convert_val(instr->copy.dst);

            struct asm_instr *mov = make_mov(src, dst);
            append_instr(fn, mov);
            break;
        }
        case IR_LABEL: {
            struct asm_instr *label = calloc(1, sizeof(struct asm_instr));
            label->type = ASM_LABEL;
            label->label.identifier = instr->label.label_id;
            append_instr(fn, label);
            break;
        }
    }
}

static struct asm_function *emit_function(struct ir_function *fn)
{
    struct asm_function *asm_fn = calloc(1, sizeof(struct asm_function));
    asm_fn->name = fn->name;
    asm_fn->name_length = fn->name_length;

    struct ir_instr *instr = fn->first;
    while (instr) {
        emit_instr(asm_fn, instr);
        instr = instr->next;
    }
    return asm_fn;
}

static struct asm_program *asm_phase1(struct ir_program *ir)
{
    struct asm_program *program = calloc(1, sizeof(struct asm_program));
    program->function = emit_function(ir->function);
    return program;
}

/* Phase 2: Replace pseduo operands with stack operands */

static void convert_pseudo(struct operand *oper, int *map, int *next_offset)
{
    if (oper->type != OPERAND_PSEUDO)
        return;
    
    int id = oper->pseudo;

    if (map[id] == 0) {
        *next_offset += 4;
        map[id] = -*next_offset;
    }

    oper->type = OPERAND_STACK;
    oper->stack = map[id];
}

static int asm_phase2(struct asm_program *program)
{
    int pseudo_map[64] = {0};
    int next_offset = 0;

    struct asm_function *function = program->function;
    struct asm_instr *instr = function->first;
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
            case ASM_SETCC:
                convert_pseudo(&instr->set_cc.oper, pseudo_map, &next_offset);
                break;
            default:
                break;
        }

        instr = instr->next;
    }

    return next_offset;
}

static struct asm_instr *replace_instr(struct asm_function *fn, struct asm_instr *prev,
        struct asm_instr *curr, struct asm_instr *first_new,
        struct asm_instr *last_new)
{
    last_new->next = curr->next;

    if (prev) prev->next = first_new;
    else fn->first = first_new;

    if (fn->last == curr) fn->last = last_new;

    free(curr);
    return last_new;
}

/* Phase 3: Insert allocate_stack, fix memory-memory operations */
static void asm_phase3(struct asm_program *program, int stack_offset)
{
    struct asm_function *fn = program->function;

    // Insert allocate_stack at the beginning of function
    struct asm_instr *alloc = calloc(1, sizeof(struct asm_instr));
    alloc->type = ASM_ALLOCSTACK;
    alloc->allocate_stack.val = stack_offset;
    alloc->next = fn->first;
    fn->first = alloc;

    struct asm_instr *prev = NULL;
    struct asm_instr *curr = fn->first;
    
    struct operand r10 = make_reg_operand(REG_R10);
    struct operand r11 = make_reg_operand(REG_R11);

    while (curr) {
        switch (curr->type) {
            
            // movl mem1, mem2 -> movl mem1, %r10d / movl %r10d, mem2
            case ASM_MOV: {
                if (curr->mov.src.type != OPERAND_STACK &&
                    curr->mov.dst.type != OPERAND_STACK) break;

                struct asm_instr *m1 = make_mov(curr->mov.src, r10);
                struct asm_instr *m2 = make_mov(r10, curr->mov.dst);
                chain_instr(m1, m2);
                curr = replace_instr(fn, prev, curr, m1, m2);
                break;
            }

            // addl/subl mem1, mem2 -> movl mem1, %r10d / movl %r10d, mem2
            case ASM_BINARY: {
                bool src_mem = curr->binary.src.type == OPERAND_STACK;
                bool dst_mem = curr->binary.dst.type == OPERAND_STACK;
                bool is_mul = curr->binary.op == ASM_IMUL;

                if (is_mul && dst_mem) {
                    // imull src, mem  ->  movl mem, %r11d / imull src, %r11d / movl %r11d, mem
                    struct asm_instr *m1 = make_mov(curr->binary.dst, r11);
                    struct asm_instr *op = calloc(1, sizeof(struct asm_instr));
                    op->type       = ASM_BINARY;
                    op->binary.op  = ASM_IMUL;
                    op->binary.src = curr->binary.src;
                    op->binary.dst = r11;
                    struct asm_instr *m2 = make_mov(r11, curr->binary.dst);
                    chain_instr(m1, op); chain_instr(op, m2);
                    curr = replace_instr(fn, prev, curr, m1, m2);
                } else if (!is_mul && src_mem && dst_mem) {
                    // addl/subl mem, mem  ->  movl src, %r10d / op %r10d, dst
                    struct asm_instr *m1 = make_mov(curr->binary.src, r10);
                    struct asm_instr *op = calloc(1, sizeof(struct asm_instr));
                    op->type       = ASM_BINARY;
                    op->binary.op  = curr->binary.op;
                    op->binary.src = r10;
                    op->binary.dst = curr->binary.dst;
                    chain_instr(m1, op);
                    curr = replace_instr(fn, prev, curr, m1, op);
                }
                break;
            }
            
            // idiv $imm -> movl $imm, %r10d / idiv %r10d
            case ASM_IDIV: {
                if (curr->idiv.idiv_operand.type != OPERAND_IMM) break;

                struct asm_instr *m1 = make_mov(curr->idiv.idiv_operand, r10);
                struct asm_instr *idiv = calloc(1, sizeof(struct asm_instr));
                idiv->type = ASM_IDIV;
                idiv->idiv.idiv_operand = r10;
                chain_instr(m1, idiv);
                curr = replace_instr(fn, prev, curr, m1, idiv);
                break;
            }
            case ASM_CMP: {
                if (curr->cmp.oper1.type == OPERAND_STACK &&
                    curr->cmp.oper2.type == OPERAND_STACK) {
                    struct asm_instr *mov = make_mov(curr->cmp.oper1, r10);
                    struct asm_instr *cmp = calloc(1, sizeof(struct asm_instr));
                    cmp->type = ASM_CMP;
                    cmp->cmp.oper1 = r10;
                    cmp->cmp.oper2 = curr->cmp.oper2;
                    chain_instr(mov, cmp);
                    curr = replace_instr(fn, prev, curr, mov, cmp);
                } else if (curr->cmp.oper2.type == OPERAND_IMM) {
                    struct asm_instr *mov = make_mov(curr->cmp.oper2, r11);
                    struct asm_instr *cmp = calloc(1, sizeof(struct asm_instr));
                    cmp->type = ASM_CMP;
                    cmp->cmp.oper1 = curr->cmp.oper1;
                    cmp->cmp.oper2 = r11;
                    chain_instr(mov, cmp);
                    curr = replace_instr(fn, prev, curr, mov, cmp);
                }
                break;
            }
            default:
                break;
        }
        prev = curr;
        curr = curr->next;
    }
}

void emit_x86(struct ir_program *ir, FILE *file)
{
    struct asm_program *program = asm_phase1(ir);
    int stack_offset = asm_phase2(program);
    asm_phase3(program, stack_offset);

    struct asm_function *function = program->function;
    struct asm_instr *instr = function->first;

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
                struct operand src = instr->mov.src;
                struct operand dst = instr->mov.dst;

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
                    case ASM_NEG: op = "negl"; break;
                    case ASM_NOT: op = "notl"; break;
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
                    case ASM_ADD: op = "addl"; break;
                    case ASM_SUB: op = "subl"; break;
                    case ASM_IMUL: op = "imull"; break;
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
                EMIT_OPERAND(file, instr->idiv.idiv_operand);
                fprintf(file, "\n");
                break;
            }
            case ASM_RET: {
                fprintf(file, "    movq     %%rbp, %%rsp\n");
                fprintf(file, "    popq     %%rbp\n");
                fprintf(file, "    ret\n");
                break;
            }
            case ASM_CMP: {
                fprintf(file, "    cmpl     ");
                EMIT_OPERAND(file, instr->cmp.oper1);
                fprintf(file, ", ");
                EMIT_OPERAND(file, instr->cmp.oper2);
                fprintf(file, "\n");
                break;
            }
            case ASM_JMP: {
                char l[10];
                sprintf(l, ".L%d", instr->jmp.identifier);
                fprintf(file, "    jmp    %s\n", l);
                break;
            }
            case ASM_JMPCC: {
                char l[10];
                sprintf(l, ".L%d", instr->jmp.identifier);
                fprintf(file, "    j%s    %s\n", cond_suffix(instr->jmp_cc.code), l);
                break;
            }
            case ASM_SETCC: {
                fprintf(file, "    set%s    ", cond_suffix(instr->set_cc.code));
                EMIT_OPERAND_8(file, instr->set_cc.oper);
                fprintf(file, "\n");
                break;
            }
            case ASM_LABEL: {
                char l[10];
                sprintf(l, ".L%d", instr->label.identifier);
                fprintf(file, "%s:\n", l);
                break;
            }
        }
        instr = instr->next;
    }
    
    fprintf(file, "\n    .section .note.GNU-stack,\"\",@progbits\n");
}
