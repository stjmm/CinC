#ifndef CINC_IR_H
#define CINC_IR_H

#include "sema.h"
#include "base/vector.h"

typedef enum {
    IR_VALUE_NONE,
    IR_VALUE_CONSTANT,
    IR_VALUE_PSEUDO,
    IR_VALUE_STATIC
} ir_value_kind;

typedef struct {
    ir_value_kind kind;
    type_t *ty;

    union {
        int64_t constant;
        uint32_t pseudo;  
        symbol_t *sym;    // Static values
    };
} ir_value_t;

typedef enum {
    IR_LABEL_TEMP,     // id from builders own counter
    IR_LABEL_BREAK,
    IR_LABEL_CONTINUE,
    IR_LABEL_CASE,
    IR_LABEL_USER      // id from STMT_LABEL
} ir_label_kind;

typedef struct {
    ir_label_kind kind;
    uint32_t id;
} ir_label_t;

typedef enum {
    IR_UNARY_NEG,
    IR_UNARY_BIT_NOT,
    IR_UNARY_LOG_NOT
} ir_unary_op;

typedef enum {
    IR_BINARY_ADD,
    IR_BINARY_SUB,
    IR_BINARY_MUL,
    IR_BINARY_DIV,
    IR_BINARY_REM,
    IR_BINARY_BIT_AND,
    IR_BINARY_BIT_OR,
    IR_BINARY_BIT_XOR,
    IR_BINARY_SHL,
    IR_BINARY_SHR,
    IR_BINARY_EQ,
    IR_BINARY_NE,
    IR_BINARY_LT,
    IR_BINARY_LE,
    IR_BINARY_GT,
    IR_BINARY_GE,
} ir_binary_op;

typedef enum {
    IR_INSTR_RETURN,
    IR_INSTR_UNARY,
    IR_INSTR_BINARY,
    IR_INSTR_COPY,
    IR_INSTR_JUMP,
    IR_INSTR_JUMP_IF_ZERO,
    IR_INSTR_JUMP_IF_NOT_ZERO,
    IR_INSTR_LABEL,
    IR_INSTR_CALL,
    IR_INSTR_CAST
} ir_instr_kind;

typedef struct ir_instr_t ir_instr_t;
struct ir_instr_t {
    ir_instr_kind kind;
    ir_instr_t *next;

    union {
        struct {
            ir_value_t src;
        } ret;

        struct {
            ir_unary_op op;
            ir_value_t src;
            ir_value_t dst;
        } unary;

        struct {
            ir_binary_op op;
            ir_value_t lhs;
            ir_value_t rhs;
            ir_value_t dst;
        } binary;

        struct {
            ir_value_t src;
            ir_value_t dst;
        } copy;

        struct {
            ir_value_t src;
            ir_value_t dst;
        } cast;

        struct {
            ir_label_t target;
        } jump;

        struct {
            ir_value_t cond;
            ir_label_t target;
        } jump_cond;

        struct {
            ir_label_t label;
        } label;

        struct {
            symbol_t *callee;
            vector args; // ir_value_t
            ir_value_t dst;
        } call;
    };
};

typedef struct ir_function_t ir_function_t;
struct ir_function_t {
    symbol_t *sym;
    vector params;
    LIST(ir_instr_t) instrs;

    ir_function_t *next;
};

typedef struct {
    LIST(ir_function_t) fns;
    size_t pseudo_count;
    const sema_result_t *sema;
} ir_program_t;

ir_program_t *ir_build(const sema_result_t *result);

#endif
