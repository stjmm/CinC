#ifndef CINC_X86_H
#define CINC_X86_H

#include <stdio.h>

#include "ir.h"

/* ASM Operands */

// Size agnostic registers
enum reg {
    REG_AX,
    REG_CX,
    REG_DX,
    REG_DI,
    REG_SI,
    REG_R8,
    REG_R9,
    REG_R10,
    REG_R11,
};

enum operand_type {
    OPERAND_IMM,
    OPERAND_REG,
    OPERAND_PSEUDO,
    OPERAND_STACK
};

struct operand {
    enum operand_type type;

    union {
        int imm;

        enum reg reg;

        int stack;      // Location on the stack (eg. -4(%rbp))

        struct {
            const char *name;
        } pseudo;     // Refers to temporary variables produced in TACKY
    };
};

/* ASM Instruction */

enum cond_code {
    COND_E,
    COND_NE,
    COND_G,
    COND_GE,
    COND_L,
    COND_LE,
};

enum asm_op {
    // Unary
    ASM_NEG,
    ASM_NOT,
    // Binary
    ASM_ADD,
    ASM_SUB,
    ASM_IMUL,
    ASM_AND,
    ASM_OR,
    ASM_XOR,
    ASM_SHL,
    ASM_SHR,
};

enum asm_instr_type { 
    ASM_MOV,
    ASM_UNARY,
    ASM_BINARY,
    ASM_CMP,
    ASM_IDIV,
    ASM_CDQ,
    ASM_JMP,
    ASM_JMPCC,
    ASM_SETCC,
    ASM_LABEL,
    ASM_ALLOCSTACK,
    ASM_DEALLOCSTACK,
    ASM_PUSH,
    ASM_CALL,
    ASM_RET,
};

struct asm_instr {
    enum asm_instr_type type;
    struct asm_instr *next;

    union {
        struct {
            struct operand src;
            struct operand dst;
        } mov;

        struct {
            enum asm_op op;
            struct operand oper;
        } unary;

        struct {
            enum asm_op op;
            struct operand src;
            struct operand dst;
        } binary;

        struct {
            struct operand lhs;
            struct operand rhs;
        } cmp;

        struct {
            struct operand oper;
        } idiv;

        struct {
            int identifier;
        } jmp;

        struct {
            enum cond_code code;
            int identifier;
        } jmpcc;

        struct {
            enum cond_code code;
            struct operand oper;
        } setcc;

        struct {
            int identifier;
        } label;

        struct {
            int val;
        } allocate_stack;

        struct {
            int val;
        } deallocate_stack;

        struct {
            struct operand oper;
        } push;

        struct {
            const char *identifier;
        } call;

        struct { } ret;
        struct { } cdq;
    };
};

/* ASM Function/Program */

struct asm_function {
    const char *name;
    struct asm_function *next;

    struct asm_instr *first;
    struct asm_instr *last;

    int stack_size;
};

struct asm_program {
    struct asm_function *functions;
};

void emit_x86(struct ir_program *ir, FILE *file);

#endif
