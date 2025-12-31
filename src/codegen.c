#include "codegen.h"
#include "base/arena.h"

extern arena_t *ir_arena;

static operand_t ir_new_imm(long value)
{
    operand_t op = {.kind = OPERAND_IMM, .imm = value };
    return op;
}

static operand_t ir_new_(reg_e reg)
{
    operand_t op = {.kind = OPERAND_REG, .reg = reg};
    return op;
}
