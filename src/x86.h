#ifndef CINC_X86_H
#define CINC_X86_H

#include "ir.h"
#include "sema.h"

#include <stdio.h>
#include <stdint.h>

#define ASM_REG_LIST                                           \
    X(REG_AX,   "rax",   "eax",   "ax",    "al",    "ah")      \
    X(REG_CX,   "rcx",   "ecx",   "cx",    "cl",    "ch")      \
    X(REG_DX,   "rdx",   "edx",   "dx",    "dl",    "dh")      \
    X(REG_DI,   "rdi",   "edi",   "di",    "dil",   "")        \
    X(REG_SI,   "rsi",   "esi",   "si",    "sil",   "")        \
    X(REG_R8,   "r8",    "r8d",   "r8w",   "r8b",   "")        \
    X(REG_R9,   "r9",    "r9d",   "r9w",   "r9b",   "")        \
    X(REG_R10,  "r10",   "r10d",  "r10w",  "r10b",  "")        \
    X(REG_R11,  "r11",   "r11d",  "r11w",  "r11b",  "")        \
    X(REG_SP,   "rsp",   "esp",   "sp",    "spl",   "")        \
    X(REG_BP,   "rbp",   "ebp",   "bp",    "bpl",   "")        \
    X(REG_BX,   "rbx",   "ebx",   "bx",    "bl",    "bh")      \
    X(REG_R12,  "r12",   "r12d",  "r12w",  "r12b",  "")        \
    X(REG_R13,  "r13",   "r13d",  "r13w",  "r13b",  "")        \
    X(REG_R14,  "r14",   "r14d",  "r14w",  "r14b",  "")        \
    X(REG_R15,  "r15",   "r15d",  "r15w",  "r15b",  "")

typedef enum {
#define X(name, q, l, w, b, h) name,
    ASM_REG_LIST
#undef X
} asm_reg;

/* Condition codes, with the suffix used by jCC and setCC */
#define ASM_COND_LIST   \
    X(COND_E,  "e")     \
    X(COND_NE, "ne")    \
    X(COND_G,  "g")     \
    X(COND_GE, "ge")    \
    X(COND_L,  "l")     \
    X(COND_LE, "le")

typedef enum {
#define X(name, suffix) name,
    ASM_COND_LIST
#undef X
} asm_cond;

/* Operators, with their name in string */
#define ASM_UNARY_OP_LIST       \
    X(ASM_UNARY_NEG, "neg")     \
    X(ASM_UNARY_NOT, "not")

#define ASM_BINARY_OP_LIST      \
    X(ASM_BINARY_ADD,  "add")   \
    X(ASM_BINARY_SUB,  "sub")   \
    X(ASM_BINARY_IMUL, "imul")  \
    X(ASM_BINARY_AND,  "and")   \
    X(ASM_BINARY_OR,   "or")    \
    X(ASM_BINARY_XOR,  "xor")   \
    X(ASM_BINARY_SAL,  "sal")   \
    X(ASM_BINARY_SAR,  "sar")

typedef enum {
#define X(name, name_str) name,
    ASM_UNARY_OP_LIST
#undef X
} asm_unary_op;

typedef enum {
#define X(name, name_str) name,
    ASM_BINARY_OP_LIST
#undef X
} asm_binary_op;

typedef enum {
    ASM_BYTE = 1,
    ASM_WORD = 2,
    ASM_LONGWORD = 4,
    ASM_QUADWORD = 8
} asm_size;

typedef enum {
    OPERAND_IMM,
    OPERAND_REG,
    OPERAND_PSEUDO,
    OPERAND_STACK,
    OPERAND_DATA
} asm_operand_kind;

typedef struct {
    asm_operand_kind kind;

    union {
        int64_t imm;
        asm_reg reg;
        uint32_t pseudo; // IR pseudo id
        int32_t stack;   // Offset from %rbp
        symbol_t *data;  // Static-duration variables
    };
} asm_operand_t;

typedef enum {
    ASM_INSTR_MOV,
    ASM_INSTR_MOVSX,
    ASM_INSTR_UNARY,
    ASM_INSTR_BINARY,
    ASM_INSTR_CMP,
    ASM_INSTR_IDIV,
    ASM_INSTR_CDQ,
    ASM_INSTR_JMP,
    ASM_INSTR_JMPCC,
    ASM_INSTR_SETCC,
    ASM_INSTR_LABEL,
    ASM_INSTR_PUSH,
    ASM_INSTR_CALL,
    ASM_INSTR_RET
} asm_instr_kind;

typedef struct asm_instr_t asm_instr_t;
struct asm_instr_t {
    asm_instr_kind kind;
    asm_instr_t *next;

    /* Operand size. Unused by jumps, labels, call and ret */
    asm_size size;

    union {
        /* ASM_INSTR_MOV and ASM_INSTR_MOVSX */
        struct {
            asm_operand_t src;
            asm_operand_t dst;
        } mov;

        struct {
            asm_unary_op op;
            asm_operand_t dst;
        } unary;

        struct {
            asm_binary_op op;
            asm_operand_t src;
            asm_operand_t dst;
        } binary;

        /* Sets the flags for dst - src */
        struct {
            asm_operand_t src;
            asm_operand_t dst;
        } cmp;

        struct {
            asm_operand_t divisor;
        } idiv;

        struct {
            ir_label_t target;
        } jmp;

        struct {
            asm_cond cond;
            ir_label_t target;
        } jmpcc;

        struct {
            asm_cond cond;
            asm_operand_t dst;
        } setcc;

        struct {
            ir_label_t label;
        } label;

        struct {
            asm_operand_t src;
        } push;

        struct {
            symbol_t *callee;
        } call;
    };
};

typedef struct asm_function_t asm_function_t;
struct asm_function_t {
    symbol_t *sym;
    LIST(asm_instr_t) instrs;
    int32_t stack_size;

    asm_function_t *next;
};

typedef struct {
    LIST(asm_function_t) fns;
    const sema_result_t *sema;
} asm_program_t;

bool asm_emit(
        ir_program_t *ir_program,
        sema_result_t *sema,
        FILE *out);

#endif
