#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include "x86.h"
#include "ir.h"

/* Phase 1: Convert TACKY IR to ASM AST. Keeps temporary variables (pseduos) */

static enum asm_op convert_unop(enum ir_unary_op op)
{
    switch (op) {
        case IR_NEGATE:     return ASM_NEG;
        case IR_COMPLEMENT: return ASM_NOT;
        default:            return ASM_NEG; // Unreachable
    }
}

static enum asm_op convert_binop(enum ir_binary_op op)
{
    switch (op) {
        case IR_ADD:         return ASM_ADD;
        case IR_SUBTRACT:    return ASM_SUB;
        case IR_MULTIPLY:    return ASM_IMUL;
        case IR_AND:         return ASM_AND;
        case IR_OR:          return ASM_OR;
        case IR_XOR:         return ASM_XOR;
        case IR_SHIFT_LEFT:  return ASM_SHL;
        case IR_SHIFT_RIGHT: return ASM_SHR;
        default:             return ASM_ADD; // Unreachable
    }
}

static enum cond_code convert_to_cond(enum ir_binary_op op)
{
    switch (op) {
        case IR_EQUAL:         return COND_E;
        case IR_NOT_EQUAL:     return COND_NE;
        case IR_LESS:          return COND_L;
        case IR_LESS_EQUAL:    return COND_LE;
        case IR_GREATER:       return COND_G;
        case IR_GREATER_EQUAL: return COND_GE;
        default:               return COND_E; // Unreachable
    }
}
static struct operand make_reg(enum reg r)
{
    return (struct operand){ .type = OPERAND_REG, .reg = r, };
}

static struct operand make_imm(int imm)
{
    return (struct operand){ .type = OPERAND_IMM, .imm = imm };
}

static struct asm_instr *alloc_instr(enum asm_instr_type type)
{
    struct asm_instr *instr = calloc(1, sizeof(struct asm_instr));
    instr->type = type;
    return instr;
}

static struct asm_instr *make_mov(struct operand src, struct operand dst)
{
    struct asm_instr *instr = alloc_instr(ASM_MOV);
    instr->mov.src = src;
    instr->mov.dst = dst;
    return instr;
}

static struct asm_instr *make_unary(enum asm_op op, struct operand dst)
{
    struct asm_instr *instr = alloc_instr(ASM_UNARY);
    instr->unary.op = op;
    instr->unary.dst = dst;
    return instr;
}

static struct asm_instr *make_binary(enum asm_op op, struct operand src,
                                     struct operand dst)
{
    struct asm_instr *instr = alloc_instr(ASM_BINARY);
    instr->binary.op = op;
    instr->binary.src = src;
    instr->binary.dst = dst;
    return instr;
}

static struct asm_instr *make_cmp(struct operand oper1, struct operand oper2)
{
    struct asm_instr *instr  = alloc_instr(ASM_CMP);
    instr->cmp.oper1 = oper1;
    instr->cmp.oper2 = oper2;
    return instr;
}

static struct asm_instr *make_idiv(struct operand oper)
{
    struct asm_instr *instr  = alloc_instr(ASM_IDIV);
    instr->idiv.oper = oper;
    return instr;
}

static struct asm_instr *make_jmp(int label_id)
{
    struct asm_instr *instr = alloc_instr(ASM_JMP);
    instr->jmp.identifier = label_id;
    return instr;
}

static struct asm_instr *make_jmpcc(enum cond_code code, int label_id)
{
    struct asm_instr *instr = alloc_instr(ASM_JMPCC);
    instr->jmpcc.code       = code;
    instr->jmpcc.identifier = label_id;
    return instr;
}

static struct asm_instr *make_setcc(enum cond_code code, struct operand oper)
{
    struct asm_instr *instr = alloc_instr(ASM_SETCC);
    instr->setcc.code = code;
    instr->setcc.oper = oper;
    return instr;
}

static struct asm_instr *make_label(int label_id)
{
    struct asm_instr *instr = alloc_instr(ASM_LABEL);
    instr->label.identifier = label_id;
    return instr;
}

static struct asm_instr *make_ret(void)   { return alloc_instr(ASM_RET); }
static struct asm_instr *make_cdq(void)   { return alloc_instr(ASM_CDQ); }

static struct operand convert_val(struct ir_val v)
{
    if (v.type == IR_VAL_CONSTANT) {
        return make_imm(v.constant);
    } else {
        return (struct operand){ .type = OPERAND_PSEUDO, .pseudo = v.var_id };
    }
}

static void append_instr(struct asm_function *fn, struct asm_instr *instr)
{
    if (!fn->last)
        fn->first = instr;
    else
        fn->last->next = instr;
    fn->last = instr;
}

static struct asm_instr *replace_instr(struct asm_function *fn,
        struct asm_instr *prev, struct asm_instr *curr,
        struct asm_instr *first_new, struct asm_instr *last_new)
{
    last_new->next = curr->next;

    if (prev)
        prev->next = first_new;
    else
        fn->first = first_new;

    if (fn->last == curr)
        fn->last = last_new;

    free(curr);
    return last_new;
}

static void emit_instr(struct asm_function *fn, struct ir_instr *instr)
{
    switch (instr->type) {
        case IR_RETURN: {
            struct operand src = convert_val(instr->ret.src);

            append_instr(fn, make_mov(src, make_reg(REG_AX)));
            append_instr(fn, make_ret());
            break;
        }
        case IR_UNARY: {
            struct operand src = convert_val(instr->unary.src);
            struct operand dst = convert_val(instr->unary.dst);

            if (instr->unary.op == IR_NOT) {
                append_instr(fn, make_cmp(make_imm(0), src));
                append_instr(fn, make_mov(make_imm(0), dst));
                append_instr(fn, make_setcc(COND_E, dst));
                break;
            }

            append_instr(fn, make_mov(src, dst));
            append_instr(fn, make_unary(convert_unop(instr->unary.op), dst));
            break;
        }
        case IR_BINARY: {
            struct operand src1 = convert_val(instr->binary.src1);
            struct operand src2 = convert_val(instr->binary.src2);
            struct operand dst = convert_val(instr->binary.dst);
            enum ir_binary_op op = instr->binary.op;

            // Converts Binary to Mov(src, AX), Cdq, Idiv(src), Mov(AX/DX, dst)
            if (op == IR_DIVIDE || op == IR_REMAINDER) {
                append_instr(fn, make_mov(src1, make_reg(REG_AX)));
                append_instr(fn, make_cdq());
                append_instr(fn, make_idiv(src2));
                append_instr(fn, make_mov(make_reg(op == IR_DIVIDE ? REG_AX : REG_DX), dst));
                break;
            } else if (op == IR_EQUAL || op == IR_NOT_EQUAL ||
                op == IR_LESS || op == IR_LESS_EQUAL ||
                op == IR_GREATER || op == IR_GREATER_EQUAL) {
                append_instr(fn, make_cmp(src2, src1));
                append_instr(fn, make_mov(make_imm(0), dst));
                append_instr(fn, make_setcc(convert_to_cond(op), dst));
                break;
            } else if (op == IR_AND || op == IR_OR || op == IR_XOR) {
                struct operand ax = make_reg(REG_AX);

                append_instr(fn, make_mov(src1, ax));
                append_instr(fn, make_binary(convert_binop(op), src2, ax));
                append_instr(fn, make_mov(ax, dst));
                break;
            } else if (instr->binary.op == IR_SHIFT_LEFT || instr->binary.op == IR_SHIFT_RIGHT) {
                struct operand ax = make_reg(REG_AX);
                struct operand count;

                append_instr(fn, make_mov(src1, ax));

                if (src2.type == OPERAND_IMM) {
                    count = src2;
                } else {
                    append_instr(fn, make_mov(src2, make_reg(REG_CX)));
                    count = make_reg(REG_CX);
                }

                append_instr(fn, make_binary(convert_binop(op), count, ax));
                append_instr(fn, make_mov(ax, dst));
                break;
            }

            // ADD, SUB, IMUL
            append_instr(fn, make_mov(src1, dst));
            append_instr(fn, make_binary(convert_binop(op), src2, dst));
            break;
        }
        case IR_JUMP_IF_ZERO: {
            struct operand val = convert_val(instr->jump_if_zero.cond);

            append_instr(fn, make_cmp(val, make_imm(0)));
            append_instr(fn, make_jmpcc(COND_E, instr->jump_if_zero.label_id));
            break;
        }
        case IR_JUMP_IF_NOT_ZERO: {
            struct operand val = convert_val(instr->jump_if_not_zero.cond);

            append_instr(fn, make_cmp(val, make_imm(0)));
            append_instr(fn, make_jmpcc(COND_NE, instr->jump_if_not_zero.label_id));
            break;
        }
        case IR_JUMP: {
            append_instr(fn, make_jmp(instr->jump.label_id));
            break;
        }
        case IR_COPY: {
            struct operand src = convert_val(instr->copy.src);
            struct operand dst = convert_val(instr->copy.dst);

            append_instr(fn, make_mov(src, dst));
            break;
        }
        case IR_LABEL: {
            append_instr(fn, make_label(instr->label.label_id));
            break;
        }
    }
}

static struct asm_function *emit_function(struct ir_function *fn)
{
    struct asm_function *asm_fn = calloc(1, sizeof(struct asm_function));
    asm_fn->name = fn->name;
    asm_fn->name_length = fn->name_length;

    for (struct ir_instr *i = fn->first; i != NULL; i = i->next) {
        emit_instr(asm_fn, i);
    }

    return asm_fn;
}

static struct asm_program *asm_phase1(struct ir_program *ir)
{
    struct asm_program *program = calloc(1, sizeof(struct asm_program));
    program->function = emit_function(ir->function);
    return program;
}

/* Phase 2: Replace pseduo operands with stack slots */

static void convert_pseudo(struct operand *oper, int *map, int *offset)
{
    if (oper->type != OPERAND_PSEUDO)
        return;
    
    int id = oper->pseudo;

    if (map[id] == 0) {
        *offset += 4;
        map[id] = -*offset;
    }

    oper->type = OPERAND_STACK;
    oper->stack = map[id];
}

static int asm_phase2(struct asm_program *program)
{
    int pseudo_map[128] = {0};
    int stack_offset = 0;

    struct asm_function *fn = program->function;
    struct asm_instr *instr = fn->first;
    while(instr) {
        switch (instr->type) {
            case ASM_MOV:
                convert_pseudo(&instr->mov.src, pseudo_map, &stack_offset);
                convert_pseudo(&instr->mov.dst, pseudo_map, &stack_offset);
                break;
            case ASM_UNARY:
                convert_pseudo(&instr->unary.dst, pseudo_map, &stack_offset);
                break;
            case ASM_BINARY:
                convert_pseudo(&instr->binary.src, pseudo_map, &stack_offset);
                convert_pseudo(&instr->binary.dst, pseudo_map, &stack_offset);
                break;
            case ASM_SETCC:
                convert_pseudo(&instr->setcc.oper, pseudo_map, &stack_offset);
                break;
            case ASM_IDIV:
                convert_pseudo(&instr->idiv.oper, pseudo_map, &stack_offset);
                break;
            case ASM_CMP:
                convert_pseudo(&instr->cmp.oper1, pseudo_map, &stack_offset);
                convert_pseudo(&instr->cmp.oper2, pseudo_map, &stack_offset);
            default:
                break;
        }

        instr = instr->next;
    }

    return stack_offset;
}


/* Phase 3: Fix illegal operator-operator combos */
static void asm_phase3(struct asm_program *program, int stack_size)
{
    struct asm_function *fn = program->function;

    // Insert allocate_stack (function prologue)
    struct asm_instr *alloc = alloc_instr(ASM_ALLOCSTACK);
    alloc->allocate_stack.val = stack_size;
    alloc->next = fn->first;
    fn->first = alloc;

    
    struct operand r10 = make_reg(REG_R10);
    struct operand r11 = make_reg(REG_R11);
    struct operand cx = make_reg(REG_CX);

    struct asm_instr *prev = NULL;
    struct asm_instr *curr = fn->first;
    while (curr) {
        switch (curr->type) {
            
            // mov mem, mem -> movl mem, r10d / movl r10d, mem
            case ASM_MOV: {
                bool src_mem = curr->mov.src.type == OPERAND_STACK;
                bool dst_mem = curr->mov.dst.type == OPERAND_STACK;

                if (src_mem && dst_mem) {
                    struct asm_instr *a = make_mov(curr->mov.src, r10);
                    struct asm_instr *b = make_mov(r10, curr->mov.dst);
                    a->next = b;
                    curr = replace_instr(fn, prev, curr, a, b);
                }

                break;
            }

            case ASM_BINARY: {
                bool src_mem = curr->binary.src.type == OPERAND_STACK;
                bool dst_mem = curr->binary.dst.type == OPERAND_STACK;
                bool is_mul = curr->binary.op == ASM_IMUL;
                bool is_shift = curr->binary.op == ASM_SHL || curr->binary.op == ASM_SHR;

                if (is_mul && dst_mem) {
                    // imull src, mem  ->  movl mem, r11d / imull src, r11d / movl r11d, mem
                    struct asm_instr *a = make_mov(curr->binary.dst, r11);
                    struct asm_instr *b = make_binary(ASM_IMUL, curr->binary.src, r11);
                    struct asm_instr *c = make_mov(r11, curr->binary.dst);
                    a->next = b; b->next = c;
                    curr = replace_instr(fn, prev, curr, a, c);
                } else if (!is_mul && src_mem && dst_mem) {
                    // op mem, mem  ->  movl src, r10d / op r10d, dst
                    struct asm_instr *a = make_mov(curr->binary.src, r10);
                    struct asm_instr *b = make_binary(curr->binary.op, r10, curr->binary.dst);
                    a->next = b;
                    curr = replace_instr(fn, prev, curr, a, b);
                } else if (is_shift && src_mem) {
                    // shll mem, dst -> movl mem, %ecx / shll %cl, dst
                    struct asm_instr *a = make_mov(curr->binary.src, cx);
                    struct asm_instr *b  = make_binary(curr->binary.op,
                                                        cx,
                                                        curr->binary.dst);
                    a->next = b;
                    curr = replace_instr(fn, prev, curr, a, b);
                }
                break;
            }
            
            // idiv $imm -> movl $imm, %r10d / idiv %r10d
            case ASM_IDIV: {
                if (curr->idiv.oper.type != OPERAND_IMM) break;

                struct asm_instr *a = make_mov(curr->idiv.oper, r10);
                struct asm_instr *b = make_idiv(r10);
                a->next = b;
                curr = replace_instr(fn, prev, curr, a, b);
                break;
            }
            case ASM_CMP: {
                if (curr->cmp.oper1.type == OPERAND_STACK &&
                    curr->cmp.oper2.type == OPERAND_STACK) {
                    struct asm_instr *a = make_mov(curr->cmp.oper1, r10);
                    struct asm_instr *b = make_cmp(r10, curr->cmp.oper2);
                    a->next = b;
                    curr = replace_instr(fn, prev, curr, a, b);
                } else if (curr->cmp.oper2.type == OPERAND_IMM) {
                    struct asm_instr *a = make_mov(curr->cmp.oper2, r11);
                    struct asm_instr *b = make_cmp(curr->cmp.oper1, r11);
                    a->next = b;
                    curr = replace_instr(fn, prev, curr, a, b);
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

static const char *reg_name_32(enum reg r)
{
    switch (r) {
        case REG_AX:  return "eax";
        case REG_CX:  return "ecx";
        case REG_DX:  return "edx";
        case REG_R10: return "r10d";
        case REG_R11: return "r11d";
        default:      return "unknown";
    }
}

static const char *reg_name_8(enum reg r)
{
    switch (r) {
        case REG_AX:  return "al";
        case REG_CX:  return "cl";
        case REG_DX:  return "dl";
        case REG_R10: return "r10b";
        case REG_R11: return "r11b";
        default:      return "unknown";
    }
}

static const char *cond_suffix(enum cond_code c)
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

static const char *asm_op_str(enum asm_op op)
{
    switch (op) {
        case ASM_ADD:  return "addl";
        case ASM_SUB:  return "subl";
        case ASM_IMUL: return "imull";
        case ASM_AND:  return "andl";
        case ASM_OR:   return "orl";
        case ASM_XOR:  return "xorl";
        case ASM_SHL:  return "shll";
        case ASM_SHR:  return "shrl";
        case ASM_NEG:  return "negl";
        case ASM_NOT:  return "notl";
        default:       return "???";
    }
}

static void emit_operand(FILE *file, struct operand op, bool use_8bit)
{
    switch (op.type) {
        case OPERAND_REG:
            fprintf(file, "%%%s", use_8bit ? reg_name_8(op.reg) : reg_name_32(op.reg));
            break;
        case OPERAND_STACK:
            fprintf(file, "%d(%%rbp)", op.stack);
            break;
        case OPERAND_IMM:
            fprintf(file, "$%d", op.imm);
        default:
            break;
    }
}

void emit_x86(struct ir_program *ir, FILE *file)
{
    struct asm_program *program = asm_phase1(ir);
    int stack_size = asm_phase2(program);
    asm_phase3(program, stack_size);

    struct asm_function *fn = program->function;
    struct asm_instr *instr = fn->first;

    fprintf(file, "    .globl %.*s\n", fn->name_length, fn->name);
    fprintf(file, "%.*s:\n", fn->name_length, fn->name);
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
                fprintf(file, "    movl     ");
                emit_operand(file, instr->mov.src, false);
                fprintf(file, ", ");
                emit_operand(file, instr->mov.dst, false);
                fprintf(file, "\n");
                break;
            }
            case ASM_UNARY: {
                fprintf(file, "    %s     ", asm_op_str(instr->unary.op));
                emit_operand(file, instr->unary.dst, false);
                fprintf(file, "\n");
                break;
            }
            case ASM_BINARY: {
                bool is_shift = instr->binary.op == ASM_SHL || instr->binary.op == ASM_SHR;
                fprintf(file, "    %s     ", asm_op_str(instr->binary.op));
                emit_operand(file, instr->binary.src, is_shift && instr->binary.src.type == OPERAND_REG);
                fprintf(file, ", ");
                emit_operand(file, instr->binary.dst, false);
                fprintf(file, "\n");
                break;
            }
            case ASM_IDIV: {
                fprintf(file, "    idivl    ");
                emit_operand(file, instr->idiv.oper, false);
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
                emit_operand(file, instr->cmp.oper1, false);
                fprintf(file, ", ");
                emit_operand(file, instr->cmp.oper2, false);
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
                sprintf(l, ".L%d", instr->jmpcc.identifier);
                fprintf(file, "    j%s    %s\n", cond_suffix(instr->jmpcc.code), l);
                break;
            }
            case ASM_SETCC: {
                fprintf(file, "    set%s    ", cond_suffix(instr->setcc.code));
                emit_operand(file, instr->setcc.oper, true);
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
