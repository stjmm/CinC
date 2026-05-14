#ifndef CINC_IR_H
#define CINC_IR_H

#include "ast.h"

/* Values */

enum ir_value_kind {
    IR_VALUE_CONSTANT,
    IR_VALUE_PSEUDO,
    IR_VALUE_STATIC,
};

struct ir_value {
    enum ir_value_kind kind;

    union {
        long constant;
        
        const char *name;
    };
};

/* Operations */

enum ir_unary_op {
    IR_UNOP_NEG,
    IR_UNOP_BIT_NOT,
    IR_UNOP_LOG_NOT
};

enum ir_binary_op {
    IR_BINOP_ADD,
    IR_BINOP_SUB,
    IR_BINOP_MUL,
    IR_BINOP_DIV,
    IR_BINOP_REM,
    IR_BINOP_BIT_AND,
    IR_BINOP_BIT_OR,
    IR_BINOP_BIT_XOR,
    IR_BINOP_SHL,
    IR_BINOP_SHR,
    IR_BINOP_EQ,
    IR_BINOP_NE,
    IR_BINOP_LT,
    IR_BINOP_LE,
    IR_BINOP_GT,
    IR_BINOP_GE,
};

enum ir_instr_kind {
    IR_INSTR_RETURN,
    IR_INSTR_UNARY,
    IR_INSTR_BINARY,
    IR_INSTR_COPY,
    IR_INSTR_JUMP,
    IR_INSTR_JUMP_IF_ZERO,
    IR_INSTR_JUMP_IF_NOT_ZERO,
    IR_INSTR_LABEL,
    IR_INSTR_CALL
};

struct ir_instr {
    enum ir_instr_kind kind;
    struct ir_instr *next;

    union {
        struct {
            struct ir_value src;
            bool has_value;
        } ret;

        struct {
            enum ir_unary_op op;
            struct ir_value src;
            struct ir_value dst;
        } unary;

        struct {
            enum ir_binary_op op;
            struct ir_value lhs;
            struct ir_value rhs;
            struct ir_value dst;
        } binary;

        struct {
            struct ir_value src;
            struct ir_value dst;
        } copy;

        struct {
            const char *calle;

            struct ir_value *args; // Array of args
            int arg_count;

            bool has_dst;
            struct ir_value dst;
        } call;

        struct {
            int label_id;
        } jump;

        struct {
            struct ir_value cond;
            int label_id;
        } jump_if_zero;

        struct {
            struct ir_value cond;
            int label_id;
        } jump_if_not_zero;

        struct {
            int label_id;
        } label;
    };
};

/* Function / Program */

struct ir_param {
    const char *name;
    struct ir_param *next;
};

struct ir_function {
    const char *name;
    enum linkage linkage;

    struct ir_param *params;

    struct ir_instr *first;
    struct ir_instr *last;

    struct ir_function *next;
};

struct ir_static_variable {
    const char *name;
    enum linkage linkage;

    int init; // Later ir_static_init

    struct ir_static_variable *next;
};

struct ir_program {
    struct ir_function *functions;
    struct ir_static_variable *static_vars;
};

struct ir_program *build_ir(struct ast_program *program);

#endif
