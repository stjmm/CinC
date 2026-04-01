#ifndef CINC_X86_H
#define CINC_X86_H

#include <stdio.h>

#include "ir.h"

/* ASM Operands */

// Size agnostic registers
typedef enum {
    REG_AX,
    REG_R10,
} asm_reg_e;

typedef enum {OPERAND_IMM, OPERAND_REG, OPERAND_PSEUDO, OPERAND_STACK } operand_type_e;

typedef struct {
    operand_type_e type;
    union {
        int imm;
        asm_reg_e reg;
        int pseudo; // Refers to temporary variables produced in TACKY
        int stack; // Location on the stack (eg. -4(%rbp))
    };
} operand_t;

/* ASM Instruction */

typedef enum { ASM_OP_NEG, ASM_OP_NOT } operator_e;

typedef enum { ASM_MOV, ASM_UNARY, ASM_ALLOCSTACK, ASM_RET } asm_type_e;

typedef struct asm_instr_t asm_instr_t;
struct asm_instr_t {
    asm_type_e type;
    asm_instr_t *next;
    union {
        struct { operand_t src; operand_t dst; } mov;
        struct { operator_e op; operand_t dst; } unary;
        struct { int val; } allocate_stack;
        struct { } ret;
    };
};

/* ASM Function/Program */

typedef struct {
    const char *name;
    int name_length;
    asm_instr_t *first;
    asm_instr_t *last;
} asm_function_t;

typedef struct {
    asm_function_t *function;
} asm_program_t;

void emit_x86(ir_program_t *ir, FILE *file);

#endif
