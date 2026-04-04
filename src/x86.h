#ifndef CINC_X86_H
#define CINC_X86_H

#include <stdio.h>

#include "ir.h"

/* ASM Operands */

// Size agnostic registers
enum reg {
    REG_AX,
    REG_DX,
    REG_R10,
    REG_R11,
};

enum operand_type { OPERAND_IMM, OPERAND_REG, OPERAND_PSEUDO, OPERAND_STACK };

struct operand {
    enum operand_type type;
    union {
        int imm;
        enum reg reg;
        int pseudo;     // Refers to temporary variables produced in TACKY
        int stack;      // Location on the stack (eg. -4(%rbp))
    };
};

/* ASM Instruction */

enum asm_op {
    // Unary
    ASM_ADD,
    ASM_SUB,
    ASM_IMUL,
    // Binary
    ASM_NEG,
    ASM_NOT,
};

enum asm_instr_type { 
    ASM_MOV,
    ASM_UNARY,
    ASM_BINARY,
    ASM_IDIV,
    ASM_CDQ,
    ASM_ALLOCSTACK,
    ASM_RET,
};

struct asm_instr {
    enum asm_instr_type type;
    struct asm_instr *next;
    union {
        struct { struct operand src; struct operand dst; } mov;
        struct { enum asm_op op; struct operand dst; } unary;
        struct { enum asm_op op; struct operand src; struct operand dst; } binary;
        struct { struct operand idiv_operand; } idiv;
        struct { } cdq;
        struct { int val; } allocate_stack;
        struct { } ret;
    };
};

/* ASM Function/Program */

struct asm_function {
    const char *name;
    int name_length;
    struct asm_instr *first;
    struct asm_instr *last;
};

struct asm_program {
    struct asm_function *function;
};

void emit_x86(struct ir_program *ir, FILE *file);

#endif
