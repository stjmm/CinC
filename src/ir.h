#ifndef CINC_IR_H
#define CINC_IR_H

#include "ast.h"

/*
 * Based on Three-Address Representation "TACKY"
 * from "Writing a C Compiler" by Nora Sandler
 */

/* IR Value */

typedef enum { IR_VAL_VAR, IR_VAL_CONSTANT } ir_val_kind_e;

typedef struct {
    ir_val_kind_e kind;
    union {
        long constant;
        int var_id;
    };
} ir_val_t;

/* IR Instructions */

typedef enum { OP_NEGATE, OP_COMPLEMENT } ir_op_e;

typedef enum { IR_RETURN, IR_UNARY } ir_type_e;

typedef struct ir_instr_t ir_instr_t;
struct ir_instr_t {
    ir_type_e type;
    ir_instr_t *next; // For linked-list of instructions in a function
    union {
        struct { ir_val_t src; } ret;
        struct { ir_op_e op; ir_val_t src; ir_val_t dst; } unary;
    };
};

/* IR Function/Program */

typedef struct {
    const char *name;
    int name_length;
    ir_instr_t *first;
    ir_instr_t *last;
} ir_function_t;

typedef struct {
    ir_function_t *function;   
} ir_program_t;

ir_program_t *tacky_build(ast_node_t *root);

#endif
