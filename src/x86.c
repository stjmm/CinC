#include "x86.h"
#include "base/memory.h"
#include "ir.h"
#include "type.h"

typedef struct {
    asm_function_t *fn; // Current function
} x86_builder_t;

static x86_builder_t x86;

static constexpr size_t ARG_REG_COUNT = 6;
static const asm_reg arg_regs[ARG_REG_COUNT] = {
    REG_DI, REG_SI, REG_DX, REG_CX, REG_R8, REG_R9
};

static asm_size
size_of(const type_t *ty)
{
    return type_size(ty) == 8 ? ASM_QUADWORD : ASM_LONGWORD;
}

static asm_unary_op
convert_unary_op(ir_unary_op op)
{
    switch (op) {
        case IR_UNARY_NEG:
            return ASM_UNARY_NEG;
        case IR_UNARY_BIT_NOT:
            return ASM_UNARY_NOT;
        default:
            (void)0;
    }
}

static asm_binary_op
convert_binary_op(ir_binary_op op)
{
    switch (op) {
        case IR_BINARY_ADD:
            return ASM_BINARY_ADD;
        case IR_BINARY_SUB:
            return ASM_BINARY_SUB;
        case IR_BINARY_MUL:
            return ASM_BINARY_IMUL;
        case IR_BINARY_BIT_AND:
            return ASM_BINARY_AND;
        case IR_BINARY_BIT_OR:
            return ASM_BINARY_OR;
        case IR_BINARY_BIT_XOR:
            return ASM_BINARY_XOR;
        case IR_BINARY_SHL:
            return ASM_BINARY_SAL;
        case IR_BINARY_SHR:
            return ASM_BINARY_SAR;
        default:
            (void)0;
    }
}

static asm_cond
convert_cond(ir_binary_op op)
{
    switch (op) {
        case IR_BINARY_EQ:
            return COND_E;
        case IR_BINARY_NE:
            return COND_NE;
        case IR_BINARY_LT:
            return COND_L;
        case IR_BINARY_LE:
            return COND_LE;
        case IR_BINARY_GT:
            return COND_G;
        case IR_BINARY_GE:
            return COND_GE;
        default:
            (void)0;
    }
}

/*
 * Operands
 */

static asm_operand_t
operand_reg(asm_reg reg)
{
    return (asm_operand_t){
        .kind = OPERAND_REG,
        .reg = reg
    };
}

static asm_operand_t
operand_imm(int64_t imm)
{
    return (asm_operand_t){
        .kind = OPERAND_IMM,
        .imm = imm
    };
}

static asm_operand_t
operand_pseudo(uint32_t pseudo)
{
    return (asm_operand_t){
        .kind = OPERAND_PSEUDO,
        .pseudo = pseudo
    };
}

static asm_operand_t
operand_stack(int32_t stack)
{
    return (asm_operand_t){
        .kind = OPERAND_STACK,
        .stack = stack
    };
}

static asm_operand_t
operand_data(symbol_t *sym)
{
    return (asm_operand_t){
        .kind = OPERAND_DATA,
        .data = sym,
    };
}

static asm_operand_t
operand_from_value(ir_value_t value)
{
    switch (value.kind) {
        case IR_VALUE_CONSTANT:
            return operand_imm(value.constant);
        case IR_VALUE_PSEUDO:
            return operand_pseudo(value.pseudo);
        case IR_VALUE_STATIC:
            return operand_data(value.sym);
        default:
            return operand_pseudo(value.pseudo);
    }
}

static asm_instr_t *
instr_new(asm_instr_kind kind, asm_size size)
{
    asm_instr_t *instr = xcalloc(1, sizeof(asm_instr_t));
    instr->kind = kind;
    instr->size = size;

    LIST_APPEND(&x86.fn->instrs, instr);
    return instr;
}

static void
emit_ret(void)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_RET, ASM_QUADWORD);
}

static void
emit_mov(asm_size size, asm_operand_t src, asm_operand_t dst)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_MOV, size);
    instr->mov.src = src;
    instr->mov.dst = dst;
}

/* Sign extends 4 bytes to 8 */
static void
emit_movsx(asm_operand_t src, asm_operand_t dst)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_MOV, ASM_QUADWORD);
    instr->mov.src = src;
    instr->mov.dst = dst;
}

static void
emit_unary(asm_size size, asm_unary_op op, asm_operand_t dst)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_UNARY, size);
    instr->unary.op = op;
    instr->unary.dst = dst;
}

static void
emit_binary(
        asm_size size,
        asm_binary_op op,
        asm_operand_t src,
        asm_operand_t dst)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_BINARY, size);
    instr->binary.op = op;
    instr->binary.src = src;
    instr->binary.dst = dst;
}

/* Sets flags for dst - src */
static void
emit_cmp(asm_size size, asm_operand_t src, asm_operand_t dst)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_CMP, size);
    instr->cmp.src = src;
    instr->cmp.dst = dst;
}

static void
emit_idiv(asm_size size, asm_operand_t divisor)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_IDIV, size);
    instr->idiv.divisor = divisor;
}

/* Sign extends %ax into %dx:%ax */
static void
emit_cdq(asm_size size)
{
    instr_new(ASM_INSTR_CDQ, size);
}

static void
emit_jmp(ir_label_t target)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_JMP, ASM_QUADWORD);
    instr->jmp.target = target;
}

static void
emit_jmpcc(asm_cond cond, ir_label_t target)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_JMPCC, ASM_QUADWORD);
    instr->jmpcc.cond = cond;
    instr->jmpcc.target = target;
}

/* Writes only the low byte of dst */
static void
emit_setcc(asm_cond cond, asm_operand_t dst)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_SETCC, ASM_BYTE);
    instr->setcc.cond = cond;
    instr->setcc.dst = dst;
}

static void
emit_label(ir_label_t label)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_LABEL, ASM_QUADWORD);
    instr->label.label = label;
}

/* x64 always pushes 8 bytes */
static void
emit_push(asm_operand_t src)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_PUSH, ASM_QUADWORD);
    instr->push.src = src;
}

static void
emit_call(symbol_t *callee)
{
    asm_instr_t *instr = instr_new(ASM_INSTR_CALL, ASM_QUADWORD);
    instr->call.callee = callee;
}

/* 
 * Phase 1: Lower ir AST to asm AST
 */

static void
lower_unary(ir_instr_t *instr)
{
    ir_value_t src = instr->unary.src;
    ir_value_t dst = instr->unary.dst;

    if (instr->unary.op == IR_UNARY_LOG_NOT) {
        // cmp $0, src
        // mov $0, dst
        // sete dst
        emit_cmp(size_of(src.ty), operand_imm(0), operand_from_value(src));
        emit_mov(size_of(dst.ty), operand_imm(0), operand_from_value(dst));
        emit_setcc(COND_E, operand_from_value(dst));
        return;
    }

    // mov src, dst
    // op dst
    emit_mov(size_of(dst.ty), operand_from_value(src), operand_from_value(dst));
    emit_unary(
            size_of(dst.ty),
            convert_unary_op(instr->unary.op),
            operand_from_value(dst));
}

static void
lower_binary(ir_instr_t *instr)
{
    ir_value_t lhs = instr->binary.lhs;
    ir_value_t rhs = instr->binary.rhs;
    ir_value_t dst = instr->binary.dst;
    ir_binary_op op = instr->binary.op;

    switch (op) {
        IR_BINARY_DIV:
        IR_BINARY_REM: {
            // idiv divides sign extended %dx:%ax (from cdq)
            // the quotient goes to %ax and remainder goes to $dx
            // mov lhs, %eax
            // cdq
            // idiv rhs
            // mov %ax/%dx, dst
            asm_size size = size_of(lhs.ty);
            asm_reg result = op == IR_BINARY_DIV ? REG_AX : REG_DX;
            emit_mov(size, operand_from_value(lhs), operand_reg(REG_AX));
            emit_cdq(size);
            emit_idiv(size, operand_from_value(rhs));
            emit_mov(size, operand_reg(result), operand_from_value(dst));
            break;
        }
        case IR_BINARY_EQ:
        case IR_BINARY_NE:
        case IR_BINARY_LT:
        case IR_BINARY_LE:
        case IR_BINARY_GT:
        case IR_BINARY_GE:
            // cmp rhs, lhs
            // mov $0, dst
            // setCC dst
            emit_cmp(
                    size_of(lhs.ty),
                    operand_from_value(rhs),
                    operand_from_value(lhs));
            emit_mov(size_of(dst.ty), operand_imm(0), operand_from_value(dst));
            emit_setcc(convert_binary_op(op), operand_from_value(dst));
            return;
        case IR_BINARY_SHL:
        case IR_BINARY_SHR: {
            // The shift count may be immediate or %cl
            asm_size size = size_of(dst.ty);
            asm_binary_op binop = convert_binary_op(op);

            emit_mov(size, operand_from_value(lhs), operand_from_value(dst));

            if (rhs.kind == IR_VALUE_CONSTANT) {
                emit_binary(
                        size,
                        binop,
                        operand_imm(rhs.constant),
                        operand_from_value(dst));
            } else {
                emit_mov(size_of(rhs.ty), operand_from_value(rhs), operand_reg(REG_CX));
                emit_binary(
                        size,
                        binop,
                        operand_reg(REG_CX),
                        operand_from_value(dst));
            }
            break;
        }
        default: {
            // mov lhs, dst
            // dst op= rhs
            asm_size size = size_of(dst.ty);
            emit_mov(size, operand_from_value(lhs), operand_from_value(dst));
            emit_binary(size, convert_binary_op(op), operand_from_value(rhs), operand_from_value(dst));
            break;
        }
    }
}

static void
lower_cast(ir_instr_t *instr)
{
    ir_value_t src = instr->cast.src;
    ir_value_t dst = instr->cast.dst;

    if (type_size(dst.ty) > type_size(src.ty)) {
        // int -> long
        emit_movsx(operand_from_value(src), operand_from_value(dst));
    } else {
        // long -> int
        emit_mov(ASM_LONGWORD, operand_from_value(src), operand_from_value(dst));
    }
}

static void
lower_call(ir_instr_t *instr)
{
    vector *args = &instr->call.args;
    size_t reg_count = args->count < ARG_REG_COUNT ? args->count : ARG_REG_COUNT;
    size_t stack_count = ARG_REG_COUNT - reg_count;

    int padding = stack_count % 2 != 0 ? 8 : 0;
    if (padding) {
        emit_binary(ASM_QUADWORD, ASM_BINARY_SUB, operand_imm(padding), operand_reg(REG_SP));
    }

    for (size_t i = args->count; i > ARG_REG_COUNT; i--) {
        ir_value_t arg = *VECTOR_GET(args, ir_value_t, i - 1);
        asm_operand_t src = operand_from_value(arg);

        emit_push(src);
    }

    for (size_t i = 0; i < reg_count; i++) {
        ir_value_t arg = *VECTOR_GET(args, ir_value_t, i);

        emit_mov(size_of(arg.ty), operand_from_value(arg), operand_reg(arg_regs[i]));
    }

    emit_call(instr->call.callee);

    int pushed = 8 * stack_count + padding;
    if (pushed) {
        emit_binary(
                ASM_QUADWORD,
                ASM_BINARY_SUB,
                operand_imm(pushed),
                operand_reg(REG_SP));
    }

    ir_value_t dst = instr->call.dst;
    if (dst.kind != IR_VALUE_NONE) {
        emit_mov(
                size_of(dst.ty),
                operand_reg(REG_AX),
                operand_from_value(dst));
    }
}

static void
lower_ir_instr(ir_instr_t *instr)
{
    switch (instr->kind) {
        case IR_INSTR_RETURN:
            if (instr->ret.src.kind != IR_VALUE_NONE) {
                emit_mov(
                        size_of(instr->ret.src.ty),
                        operand_from_value(instr->ret.src),
                        operand_reg(REG_AX));
            }

            emit_ret();
            break;
        case IR_INSTR_UNARY:
            lower_unary(instr);
            break;
        case IR_INSTR_BINARY:
            lower_binary(instr);
            break;
        case IR_INSTR_COPY:
            ir_value_t dst = instr->copy.dst;

            emit_mov(
                    size_of(dst.ty),
                    operand_from_value(instr->copy.src),
                    operand_from_value(dst));
            break;
        case IR_INSTR_CAST:
            lower_cast(instr);
            break;
        case IR_INSTR_JUMP:
            emit_jmp(instr->jump.target);
            break;
        case IR_INSTR_JUMP_IF_ZERO:
        case IR_INSTR_JUMP_IF_NOT_ZERO:
            ir_value_t cond = instr->jump_cond.cond;

            emit_cmp(size_of(cond.ty), operand_imm(0), operand_from_value(cond));
            emit_jmpcc(
                    instr->kind == IR_INSTR_JUMP_IF_ZERO ? COND_E : COND_NE,
                    instr->jump.target);
            break;
        case IR_INSTR_CALL:
            lower_call();
            break;
    }
}

static void
lower_params(ir_function_t *ir_fn)
{
    for (size_t i = 0; i < ir_fn->params.count; i++)  {
        ir_value_t param = *VECTOR_GET(&ir_fn->params, ir_value_t, i);
        asm_operand_t src;

        if (i < ARG_REG_COUNT) {
            src = operand_reg(arg_regs[i]);
        } else {
            // Rest of args passed on stack
            src = operand_stack(16 + 8 * (int32_t)(i - ARG_REG_COUNT));
        }

        emit_mov(size_of(param.ty), src, operand_from_value(param));
    }
}

static asm_function_t *
lower_ir_function(ir_function_t *ir_fn)
{
    asm_function_t *function = xcalloc(1, sizeof(asm_function_t));
    function->sym = ir_fn->sym;

    x86.fn = function;

    lower_params(ir_fn);

    LIST_FOREACH(ir_instr, &ir_fn->instrs) {
        lower_ir_instr(ir_instr);
    }

    x86.fn = nullptr;

    return function;
}

static asm_program_t *
lower_ir_program(ir_program_t *ir_program)
{
    asm_program_t *program = xcalloc(1, sizeof(asm_program_t));
    program->sema = ir_program->sema;

    LIST_FOREACH(ir_fn, &ir_program->fns) {
        asm_function_t *asm_fn = lower_ir_function(ir_fn);
        LIST_APPEND(&program->fns, asm_fn);
    }

    return program;
}

bool
asm_emit(
        ir_program_t *ir_program,
        sema_result_t *sema,
        FILE *out)
{
}
