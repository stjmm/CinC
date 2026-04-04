#ifndef CINC_IR_H
#define CINC_IR_H

#include "ast.h"

/*
 * Based on Three-Address Representation "TACKY"
 * from "Writing a C Compiler" by Nora Sandler
 */

/* IR Value */

enum ir_val_type { IR_VAL_VAR, IR_VAL_CONSTANT };

struct ir_val {
    enum ir_val_type type;
    union {
        long constant;
        int var_id;
    };
};

/* IR Instructions */

enum ir_unary_op { IR_NEGATE, IR_COMPLEMENT };
enum ir_binary_op { IR_ADD, IR_SUBTRACT, IR_MULTIPLY, IR_DIVIDE, IR_REMAINDER };
enum ir_instr_type { IR_RETURN, IR_UNARY, IR_BINARY };

struct ir_instr {
    enum ir_instr_type type;
    struct ir_instr *next;
    union {
        struct { struct ir_val src; } ret;
        struct {
            enum ir_unary_op op;
            struct ir_val src;
            struct ir_val dst;
        } unary;
        struct {
            enum ir_binary_op op;
            struct ir_val src1;
            struct ir_val src2;
            struct ir_val dst;
        } binary;
    };
};

/* IR Function/Program */

struct ir_function {
    const char *name;
    int name_length;
    struct ir_instr *first;
    struct ir_instr *last;
};

struct ir_program {
    struct ir_function *function;
};

struct ir_program *tacky_build(struct ast_node *root);

#endif
