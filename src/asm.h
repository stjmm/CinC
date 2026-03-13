#ifndef ASM_CINC_H
#define ASM_CINC_H

#include <stdio.h>

#include "ast.h"

typedef enum {
    OPERAND_REG,
    OPERAND_IMM,
} operand_kind_e;

typedef enum {
    REG_EAX,
} reg_e;

typedef struct operand_t operand_t;
struct operand_t {
    operand_kind_e kind;
    union {
        struct { reg_e reg; } reg;
        struct { long value; } imm;
    };
};

typedef enum {
    INSTR_MOV,
    INSTR_RET,
} instr_kind_e;

typedef struct instr_t instr_t;
struct instr_t {
    instr_kind_e kind;
    instr_t *next;
    union {
        struct { operand_t src, dst; } mov;
        // Ret carries no data
    };
};

typedef struct {
    const char *name;
    int name_length;
    instr_t *first;
} asm_function_t;

typedef struct {
    asm_function_t *first;
} asm_program_t;

void codegen(ast_node_t *root, FILE *file);

#endif
