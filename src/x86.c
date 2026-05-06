/*
 * Tacky IR -> x86-64 Assembly (AT&T Syntax)
 *
 * Phase 1: Convert IR instructions into ASM instructions
 *          keeps pseudo (temporary) operands
 *
 * Phase 2: Replace every pseudo operand with a stack slot.
 *          Returns total needed stack size
 *
 * Phase 3: Function prologue, rewrite any illegal x86 ops.
 */
#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "x86.h"
#include "ir.h"
#include "base/hash_map.h"

#define STACK_SLOT_SIZE 4
#define ARG_REG_COUNT 6

static const enum reg arg_regs[] = {
    REG_DI,
    REG_SI,
    REG_DX,
    REG_CX,
    REG_R8,
    REG_R9
};

/* Phase 1: Build ASM AST from IR AST */

static enum asm_op convert_unop(enum ir_unary_op op)
{
    switch (op) {
        case IR_UNOP_NEG:     return ASM_NEG;
        case IR_UNOP_BIT_NOT: return ASM_NOT;
        default:            return ASM_NEG; // Unreachable
    }
}

static enum asm_op convert_binop(enum ir_binary_op op)
{
    switch (op) {
        case IR_BINOP_ADD:     return ASM_ADD;
        case IR_BINOP_SUB:     return ASM_SUB;
        case IR_BINOP_MUL:     return ASM_IMUL;
        case IR_BINOP_BIT_AND: return ASM_AND;
        case IR_BINOP_BIT_OR:  return ASM_OR;
        case IR_BINOP_BIT_XOR: return ASM_XOR;
        case IR_BINOP_SHL:     return ASM_SHL;
        case IR_BINOP_SHR:     return ASM_SHR;
        default:             return ASM_ADD; // Unreachable
    }
}

static enum cond_code convert_to_cond(enum ir_binary_op op)
{
    switch (op) {
        case IR_BINOP_EQ: return COND_E;
        case IR_BINOP_NE: return COND_NE;
        case IR_BINOP_LT: return COND_L;
        case IR_BINOP_LE: return COND_LE;
        case IR_BINOP_GT: return COND_G;
        case IR_BINOP_GE: return COND_GE;
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

static struct operand make_stack(int offset)
{
    return (struct operand){ .type = OPERAND_STACK, .stack = offset };
}

static struct operand make_pseudo(const char *name)
{
    return (struct operand){ .type = OPERAND_PSEUDO, .pseudo.name = name };
}

static struct asm_instr *new_instr(enum asm_instr_type type)
{
    struct asm_instr *instr = calloc(1, sizeof(struct asm_instr));
    instr->type = type;
    return instr;
}

static struct asm_instr *make_mov(struct operand src, struct operand dst)
{
    struct asm_instr *instr = new_instr(ASM_MOV);
    instr->mov.src = src;
    instr->mov.dst = dst;
    return instr;
}

static struct asm_instr *make_unary(enum asm_op op, struct operand dst)
{
    struct asm_instr *instr = new_instr(ASM_UNARY);
    instr->unary.op = op;
    instr->unary.oper = dst;
    return instr;
}

static struct asm_instr *make_binary(enum asm_op op, struct operand src,
                                     struct operand dst)
{
    struct asm_instr *instr = new_instr(ASM_BINARY);
    instr->binary.op = op;
    instr->binary.src = src;
    instr->binary.dst = dst;
    return instr;
}

static struct asm_instr *make_cmp(struct operand oper1, struct operand oper2)
{
    struct asm_instr *instr  = new_instr(ASM_CMP);
    instr->cmp.lhs = oper1;
    instr->cmp.rhs = oper2;
    return instr;
}

static struct asm_instr *make_idiv(struct operand oper)
{
    struct asm_instr *instr  = new_instr(ASM_IDIV);
    instr->idiv.oper = oper;
    return instr;
}

static struct asm_instr *make_jmp(int label_id)
{
    struct asm_instr *instr = new_instr(ASM_JMP);
    instr->jmp.identifier = label_id;
    return instr;
}

static struct asm_instr *make_jmpcc(enum cond_code code, int label_id)
{
    struct asm_instr *instr = new_instr(ASM_JMPCC);
    instr->jmpcc.code       = code;
    instr->jmpcc.identifier = label_id;
    return instr;
}

static struct asm_instr *make_setcc(enum cond_code code, struct operand oper)
{
    struct asm_instr *instr = new_instr(ASM_SETCC);
    instr->setcc.code = code;
    instr->setcc.oper = oper;
    return instr;
}

static struct asm_instr *make_label(int label_id)
{
    struct asm_instr *instr = new_instr(ASM_LABEL);
    instr->label.identifier = label_id;
    return instr;
}

static struct asm_instr *make_alloc_stack(int value)
{
    struct asm_instr *instr = new_instr(ASM_ALLOCSTACK);
    instr->allocate_stack.val = value;
    return instr;
}

static struct asm_instr *make_dealloc_stack(int value)
{
    struct asm_instr *instr = new_instr(ASM_DEALLOCSTACK);
    instr->deallocate_stack.val = value;
    return instr;
}

static struct asm_instr *make_push(struct operand oper)
{
    struct asm_instr *instr = new_instr(ASM_PUSH);
    instr->push.oper = oper;
    return instr;
}

static struct asm_instr *make_ret(void)   { return new_instr(ASM_RET); }
static struct asm_instr *make_cdq(void)   { return new_instr(ASM_CDQ); }

// Convert 'ir_val' to ASM operand (immediate or pseudo)
static struct operand convert_val(struct ir_value val)
{
    if (val.kind == IR_VALUE_CONSTANT)
        return make_imm(val.constant);
    else
        return make_pseudo(val.name);
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

static void lower_ir_instr(struct asm_function *fn, struct ir_instr *instr)
{
    switch (instr->kind) {
        case IR_INSTR_RETURN: {
            // return val -> movl val, %eax
            if (instr->ret.has_value) {
                struct operand src = convert_val(instr->ret.src);
                append_instr(fn, make_mov(src, make_reg(REG_AX)));
            }

            append_instr(fn, make_ret());
            break;
        }
        case IR_INSTR_CALL: {
            int arg_count = instr->call.arg_count;

            int stack_arg_count = 0;
            if (arg_count > ARG_REG_COUNT)
                stack_arg_count = arg_count - ARG_REG_COUNT;

            /*
             * Keep the stack 16-byte aligned before the call.
             *
             * At this point stack is 16-byte aligned.
             * If we push uneven number of stack args (8 byte)
             * align to 16 byte again.
             */
            int padding = 0;
            if (stack_arg_count % 2 != 0)
                padding = 8;

            if (padding)
                append_instr(fn, make_alloc_stack(padding));

            // Push stack args right-to-left
            for (int i = arg_count - 1; i >= ARG_REG_COUNT; i--) {
                struct operand arg = convert_val(instr->call.args[i]);
                append_instr(fn, make_push(arg));
            }

            for (int i = 0; i < arg_count && i < ARG_REG_COUNT; i++) {
                struct operand arg = convert_val(instr->call.args[i]);
                append_instr(fn, make_mov(arg, make_reg(arg_regs[i])));
            }

            struct asm_instr *call = new_instr(ASM_CALL);
            call->call.identifier = instr->call.calle;
            append_instr(fn, call);

            int bytes_to_remove = 8 * stack_arg_count + padding;
            if (bytes_to_remove)
                append_instr(fn, make_dealloc_stack(bytes_to_remove));

            if (instr->call.has_dst) {
                struct operand dst = convert_val(instr->call.dst);
                append_instr(fn, make_mov(make_reg(REG_AX), dst));
            }
            break;
        }
        case IR_INSTR_UNARY: {
            struct operand src = convert_val(instr->unary.src);
            struct operand dst = convert_val(instr->unary.dst);

            if (instr->unary.op == IR_UNOP_LOG_NOT) {
                /*
                * Logical NOT: compare src to zero, set dst to the zero flag
                * cmpl $0, src
                * movl $0, dst
                * sete dst
                */
                append_instr(fn, make_cmp(make_imm(0), src));
                append_instr(fn, make_mov(make_imm(0), dst));
                append_instr(fn, make_setcc(COND_E, dst));
                break;
            }

            // Bitwise NOT, Arithmetic negation
            // movl src, dst
            // op dst
            append_instr(fn, make_mov(src, dst));
            append_instr(fn, make_unary(convert_unop(instr->unary.op), dst));
            break;
        }
        case IR_INSTR_BINARY: {
            struct operand src1 = convert_val(instr->binary.lhs);
            struct operand src2 = convert_val(instr->binary.rhs);
            struct operand dst = convert_val(instr->binary.dst);
            enum ir_binary_op op = instr->binary.op;

            if (op == IR_BINOP_DIV || op == IR_BINOP_REM) {
                /*
                 * Signed division: x86 IDIV divides EDX:EAX by the operand
                 * movl src1, %eax
                 * cdq              <- sign extend EAX into EDX:EAX
                 * idivl src2
                 * movl %eax/%edx, dst (quotient/remainder)
                 */
                append_instr(fn, make_mov(src1, make_reg(REG_AX)));
                append_instr(fn, make_cdq());
                append_instr(fn, make_idiv(src2));
                append_instr(fn, make_mov(make_reg(op == IR_BINOP_DIV ? REG_AX : REG_DX), dst));
                break;
            }

            if (op == IR_BINOP_EQ || op == IR_BINOP_NE ||
                op == IR_BINOP_LT || op == IR_BINOP_LE ||
                op == IR_BINOP_GT || op == IR_BINOP_GE) {
                /*
                 * Compare, zero dest, then set
                 * cmpl src2, src1
                 * movl $0, dst
                 * setcc dst
                 */
                append_instr(fn, make_cmp(src2, src1));
                append_instr(fn, make_mov(make_imm(0), dst));
                append_instr(fn, make_setcc(convert_to_cond(op), dst));
                break;
            }

            if (op == IR_BINOP_BIT_AND ||
                op == IR_BINOP_BIT_OR ||
                op == IR_BINOP_BIT_XOR) {
                /*
                 * Bitwise ops route through %eax
                 * movl src1, %eax
                 * op src2, %eax
                 * mov %eax, dst
                 */
                struct operand ax = make_reg(REG_AX);

                append_instr(fn, make_mov(src1, ax));
                append_instr(fn, make_binary(convert_binop(op), src2, ax));
                append_instr(fn, make_mov(ax, dst));
                break;
            } 

            if (op == IR_BINOP_SHL || op == IR_BINOP_SHR) {
                // Shifts: count must be immediate or %cx
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
            // movl src1, dst
            // op src2, dst
            append_instr(fn, make_mov(src1, dst));
            append_instr(fn, make_binary(convert_binop(op), src2, dst));
            break;
        }
        case IR_INSTR_JUMP_IF_ZERO: {
            // if (!cond) goto label -> cmpl $0, val, je
            struct operand val = convert_val(instr->jump_if_zero.cond);

            append_instr(fn, make_cmp(val, make_imm(0)));
            append_instr(fn, make_jmpcc(COND_E, instr->jump_if_zero.label_id));
            break;
        }
        case IR_INSTR_JUMP_IF_NOT_ZERO: {
            // if (cond) goto label -> cmpl $0, val, jne
            struct operand val = convert_val(instr->jump_if_not_zero.cond);

            append_instr(fn, make_cmp(val, make_imm(0)));
            append_instr(fn, make_jmpcc(COND_NE, instr->jump_if_not_zero.label_id));
            break;
        }
        case IR_INSTR_JUMP: {
            append_instr(fn, make_jmp(instr->jump.label_id));
            break;
        }
        case IR_INSTR_COPY: {
            struct operand src = convert_val(instr->copy.src);
            struct operand dst = convert_val(instr->copy.dst);

            append_instr(fn, make_mov(src, dst));
            break;
        }
        case IR_INSTR_LABEL: {
            append_instr(fn, make_label(instr->label.label_id));
            break;
        }
    }
}

static void lower_ir_params(struct asm_function *asm_fn, struct ir_function *ir_fn)
{
    int i = 0;
    for (struct ir_param *param = ir_fn->params; param; param = param->next) {
        struct operand dst = make_pseudo(param->name);
        struct operand src;

        if (i < ARG_REG_COUNT) {
            src = make_reg(arg_regs[i]);
        } else {
            /*
             * Stack args:
             *
             * 8(%rbp) is return address of calle, so:
             *  16(%rbp) = 7th integer argument
             *  24(&rbp) = 8th...
             */
            int stack_offset = 16 + 8 * (i - ARG_REG_COUNT);
            src = make_stack(stack_offset);
        }

        append_instr(asm_fn, make_mov(src, dst));
        i++;
    }
}

static struct asm_function *lower_ir_function(struct ir_function *ir_fn)
{
    struct asm_function *asm_fn = calloc(1, sizeof(struct asm_function));
    asm_fn->name = ir_fn->name;

    lower_ir_params(asm_fn, ir_fn);

    for (struct ir_instr *i = ir_fn->first; i != NULL; i = i->next) {
        lower_ir_instr(asm_fn, i);
    }

    return asm_fn;
}

static struct asm_program *lower_ir_program(struct ir_program *ir)
{
    struct asm_program *program = calloc(1, sizeof(struct asm_program));

    struct asm_function *head = NULL;
    struct asm_function *tail = NULL;

    for (struct ir_function *ir_fn = ir->functions; ir_fn; ir_fn = ir_fn->next) {
        struct asm_function *asm_fn = lower_ir_function(ir_fn);

        if (!head)
            head = asm_fn;
        else
            tail->next = asm_fn;
        tail = asm_fn;
    }

    program->functions = head;

    return program;
}

/* Phase 2: Replace pseduo operands with RBP-relative stack slots */
struct pseudo_entry {
    const char *name; // Pseudo from IR
    int stack_offset; // Negative offset from %rbp
};

struct pseudo_map {
    hash_map entries;
    int current_offset;
};

static int pseudo_map_get_or_insert(struct pseudo_map *pm, const char *name)
{
    struct pseudo_entry *entry = hashmap_get(&pm->entries, name, strlen(name));
    if (entry)
        return entry->stack_offset;

    pm->current_offset -= STACK_SLOT_SIZE;

    entry = malloc(sizeof(struct pseudo_entry));
    entry->name = name;
    entry->stack_offset = pm->current_offset;

    hashmap_set(&pm->entries, name, strlen(name), entry);

    return pm->current_offset;
}

static void replace_pseudo(struct operand *oper, struct pseudo_map *pm)
{
    if (oper->type != OPERAND_PSEUDO)
        return;

    int offset = pseudo_map_get_or_insert(pm, oper->pseudo.name);
    oper->type  = OPERAND_STACK;
    oper->stack = offset;
}

/*
 * Replace every pseudo variable with a stack operand.
 */
static int assign_stack_slots(struct asm_function *fn)
{
    struct pseudo_map pm = {0};
    hashmap_init(&pm.entries);

    for (struct asm_instr *instr = fn->first; instr; instr = instr->next) {
        switch (instr->type) {
            case ASM_MOV:
                replace_pseudo(&instr->mov.src, &pm);
                replace_pseudo(&instr->mov.dst, &pm);
                break;
            case ASM_UNARY:
                replace_pseudo(&instr->unary.oper, &pm);
                break;
            case ASM_BINARY:
                replace_pseudo(&instr->binary.src, &pm);
                replace_pseudo(&instr->binary.dst, &pm);
                break;
            case ASM_SETCC:
                replace_pseudo(&instr->setcc.oper, &pm);
                break;
            case ASM_IDIV:
                replace_pseudo(&instr->idiv.oper, &pm);
                break;
            case ASM_CMP:
                replace_pseudo(&instr->cmp.lhs, &pm);
                replace_pseudo(&instr->cmp.rhs, &pm);
                break;
            case ASM_PUSH:
                replace_pseudo(&instr->push.oper, &pm);
            default:
                break;
        }
    }

    int raw_size = -pm.current_offset;
    hashmap_free(&pm.entries);

    return raw_size;
}

static int align_to(int value, int align)
{
    return (value + (align - 1)) / align * align;
}

static void asm_phase2(struct asm_program *program)
{
    for (struct asm_function *fn = program->functions; fn; fn = fn->next) {
        int stack_size = assign_stack_slots(fn);

        /*
         * Keep the stack frame 16 byte aligned.
         * System-V ABI.
         */
        fn->stack_size = align_to(stack_size, 16);
    }
}


/* Phase 3: Fix illegal operator-operator combos */
/*
 * x86 contrains addressed here:
 * MOV mem, mem -> MOV mem, %r10d / MOV %r10d, mem
 * IMUL src, mem -> MOV mem, %r11d / IMUL src, %r11d / MOV %r11d, mem
 * <op> mem, mem -> MOV src, %r10d / <op> %r10d, dst
 * SHIFT mem, dst -> MOV src, %ecx / SHIFT %cl, dst
 * IDIV $imm -> MOV $imm, %r10d / IDIV %r10d
 * CMP mem, mem -> MOV oper1, %r10d / CMP %r10d, oper2
 * CMP oper1, $imm -> MOV $imm, %r11d / CMP oper1, %r11d
 */
static void asm_phase3(struct asm_function *fn)
{
    /*
     * Insert stack allocation for function prologue.
     * Stack size calculated in phase 2.
     */
    if (fn->stack_size > 0) {
        struct asm_instr *instr = make_alloc_stack(fn->stack_size);

        instr->next = fn->first;
        fn->first = instr;

        if (!fn->last)
            fn->last = instr;
    }
    
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
                if (curr->cmp.lhs.type == OPERAND_STACK &&
                    curr->cmp.rhs.type == OPERAND_STACK) {
                    struct asm_instr *a = make_mov(curr->cmp.lhs, r10);
                    struct asm_instr *b = make_cmp(r10, curr->cmp.rhs);
                    a->next = b;
                    curr = replace_instr(fn, prev, curr, a, b);
                } else if (curr->cmp.rhs.type == OPERAND_IMM) {
                    struct asm_instr *a = make_mov(curr->cmp.rhs, r11);
                    struct asm_instr *b = make_cmp(curr->cmp.lhs, r11);
                    a->next = b;
                    curr = replace_instr(fn, prev, curr, a, b);
                }
                break;
            }
            
            case ASM_PUSH: {
                if (curr->push.oper.type == OPERAND_STACK) {
                    struct asm_instr *a = make_mov(curr->push.oper, r10);
                    struct asm_instr *b = make_push(r10);

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

static const char *reg_name_8(enum reg r)
{
    switch (r) {
        case REG_AX:  return "al";
        case REG_CX:  return "cl";
        case REG_DX:  return "dl";
        case REG_DI:  return "dil";
        case REG_SI:  return "sil";
        case REG_R8: return  "r8b";
        case REG_R9: return  "r9b";
        case REG_R10: return "r10b";
        case REG_R11: return "r11b";
        default:      return "unknown";
    }
}

static const char *reg_name_32(enum reg r)
{
    switch (r) {
        case REG_AX:  return "eax";
        case REG_CX:  return "ecx";
        case REG_DX:  return "edx";
        case REG_DI:  return "edi";
        case REG_SI:  return "esi";
        case REG_R8: return  "r8d";
        case REG_R9: return  "r9d";
        case REG_R10: return "r10d";
        case REG_R11: return "r11d";
        default:      return "unknown";
    }
}

static const char *reg_name_64(enum reg r)
{
    switch (r) {
        case REG_AX:  return "rax";
        case REG_CX:  return "rcx";
        case REG_DX:  return "rdx";
        case REG_DI:  return "rdi";
        case REG_SI:  return "rsi";
        case REG_R8: return  "r8";
        case REG_R9: return  "r9";
        case REG_R10: return "r10";
        case REG_R11: return "r11";
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
        case ASM_SHR:  return "sarl";
        case ASM_NEG:  return "negl";
        case ASM_NOT:  return "notl";
        default:       return "???";
    }
}

static void write_operand(FILE *file, struct operand op, int reg_size)
{
    switch (op.type) {
        case OPERAND_REG:
            if (reg_size == 8)
                fprintf(file, "%%%s", reg_name_8(op.reg));
            else if (reg_size == 32)
                fprintf(file, "%%%s", reg_name_32(op.reg));
            else if (reg_size == 64)
                fprintf(file, "%%%s", reg_name_64(op.reg));
            break;
        case OPERAND_STACK:
            fprintf(file, "%d(%%rbp)", op.stack);
            break;
        case OPERAND_IMM:
            fprintf(file, "$%d", op.imm);
            break;
        default:
            break;
    }
}

void emit_function(struct asm_function *fn, FILE *file)
{
    fprintf(file, "    .globl %s\n", fn->name);
    fprintf(file, "%s:\n", fn->name);
    fprintf(file, "    pushq    %%rbp\n");
    fprintf(file, "    movq     %%rsp, %%rbp\n");

    for (struct asm_instr *instr = fn->first; instr; instr = instr->next) {
        switch (instr->type) {
            case ASM_ALLOCSTACK:
                fprintf(file, "    subq     $%d, %%rsp\n", instr->allocate_stack.val);
                break;
            case ASM_DEALLOCSTACK:
                fprintf(file, "    addq     $%d, %%rsp\n", instr->deallocate_stack.val);
                break;
            case ASM_PUSH:
                fprintf(file, "    pushq    ");
                write_operand(file, instr->push.oper, 64);
                fprintf(file, "\n");
                break;
            case ASM_CALL:
                // TODO: Add @PLT
                fprintf(file, "    call     %s\n", instr->call.identifier);
                break;
            case ASM_CDQ:
                fprintf(file, "    cdq\n");
                break;
            case ASM_MOV:
                fprintf(file, "    movl     ");
                write_operand(file, instr->mov.src, 32);
                fprintf(file, ", ");
                write_operand(file, instr->mov.dst, 32);
                fprintf(file, "\n");
                break;
            case ASM_UNARY:
                fprintf(file, "    %s     ", asm_op_str(instr->unary.op));
                write_operand(file, instr->unary.oper, 32);
                fprintf(file, "\n");
                break;
            case ASM_BINARY: {
                bool is_shift = instr->binary.op == ASM_SHL || instr->binary.op == ASM_SHR;
                bool is_reg = instr->binary.src.type == OPERAND_REG;
                fprintf(file, "    %s     ", asm_op_str(instr->binary.op));
                write_operand(file, instr->binary.src, is_shift && is_reg ? 8 : 32);
                fprintf(file, ", ");
                write_operand(file, instr->binary.dst, 32);
                fprintf(file, "\n");
                break;
            }
            case ASM_IDIV:
                fprintf(file, "    idivl    ");
                write_operand(file, instr->idiv.oper, 32);
                fprintf(file, "\n");
                break;
            case ASM_RET:
                fprintf(file, "    movq     %%rbp, %%rsp\n");
                fprintf(file, "    popq     %%rbp\n");
                fprintf(file, "    ret\n");
                break;
            case ASM_CMP:
                fprintf(file, "    cmpl     ");
                write_operand(file, instr->cmp.lhs, 32);
                fprintf(file, ", ");
                write_operand(file, instr->cmp.rhs, 32);
                fprintf(file, "\n");
                break;
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
            case ASM_SETCC:
                fprintf(file, "    set%s    ", cond_suffix(instr->setcc.code));
                write_operand(file, instr->setcc.oper, 8);
                fprintf(file, "\n");
                break;
            case ASM_LABEL: {
                char l[10];
                sprintf(l, ".L%d", instr->label.identifier);
                fprintf(file, "%s:\n", l);
                break;
            }
        }
    }
}

void emit_x86(struct ir_program *ir, FILE *file)
{
    struct asm_program *program = lower_ir_program(ir);
    asm_phase2(program);
    for (struct asm_function *fn = program->functions; fn; fn = fn->next)
        asm_phase3(fn);

    for (struct asm_function *fn = program->functions; fn; fn = fn->next) {
        emit_function(fn, file);
    }

    // Linux/ELF requirement
    fprintf(file, "\n    .section .note.GNU-stack,\"\",@progbits\n");
}
