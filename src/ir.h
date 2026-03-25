#ifndef CINC_IR_H
#define CINC_IR_H

#include "ast.h"

/*
 * This IR is based on the "Tacky IR" defined in the book "Writing a Compiler in C"
 * by Nora Sandler
 */

typedef enum {
    IR_RETURN,
    IR_UNARY,
} ir_type_e;

typedef enum {
    OP_NEGATE,
    OP_COMPLEMENT,
} ir_op_e;

typedef struct {
    union {
        long constant;
        char *variable_name;
    };
} ir_val_t;

typedef struct {
    ir_type_e type;
    union {
        struct { ir_val_t src; } ret;
        struct {
            ir_op_e op;
            ir_val_t src;
            ir_val_t dst;
        } unary;
    };
} ir_instr_t;

typedef struct {
    const char *name;
    int name_length;
    ir_instr_t *first;
} ir_function_t;

typedef struct {
    ir_function_t *fun;   
} ir_program_t;

ir_program_t *tacky_gen(ast_node_t *root);

#endif
