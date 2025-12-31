#ifndef CINC_CODEGEN_H
#define CINC_CODEGEN_H

typedef enum {
    IR_PROGRAM,
    IR_FUNCTION,
    IR_MOV,
    IR_RET,
} ir_node_kind_e;

typedef enum {
    OPERAND_IMM,
    OPERAND_REG,
} operand_kind_e;

typedef enum {
    REG_AX,
} reg_e;

typedef struct {
    operand_kind_e kind;
    union {
        long imm;
        reg_e reg;
    };
} operand_t;

typedef struct ir_node_t ir_node_t;

struct ir_node_t {
    ir_node_kind_e kind;
    union {
        struct {
            ir_node_t *program;
        } program;

        struct {
            const char *name;
            unsigned name_len;
            ir_node_t **instructions;
            unsigned int instr_count;
            unsigned int instr_capacity;
        } function;

        struct {
            operand_t src;
            operand_t dest;
        } mov;

        struct {

        } ret;
    };
};

#endif
