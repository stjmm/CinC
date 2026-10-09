#include "x86.h"
#include "base/memory.h"
#include "ir.h"
#include "sema.h"
#include "type.h"

typedef struct {
    asm_function_t *fn; // Current function

    // Indexed by pseudo(id) used in phase 2
    int *pseudo_size;
    int *pseudo_offset;
    int frame_size; // Bytes of slots handed out in the current function
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
            // Phase 2 needs the size to give a pseudo variable a stack slot
            x86.pseudo_size[value.pseudo] = type_size(value.ty);
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
    instr_new(ASM_INSTR_RET, ASM_QUADWORD);
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
    asm_instr_t *instr = instr_new(ASM_INSTR_MOVSX, ASM_QUADWORD);
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
        case IR_BINARY_DIV:
        case IR_BINARY_REM: {
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
            emit_setcc(convert_cond(op), operand_from_value(dst));
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
    size_t stack_count = args->count - reg_count;

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
                ASM_BINARY_ADD,
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
                    instr->jump_cond.target);
            break;
        case IR_INSTR_CALL:
            lower_call(instr);
            break;
        case IR_INSTR_LABEL:
            emit_label(instr->label.label);
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

    x86.pseudo_size = xcalloc(ir_program->pseudo_count, sizeof(int));
    x86.pseudo_offset = xcalloc(ir_program->pseudo_count, sizeof(int));

    LIST_FOREACH(ir_fn, &ir_program->fns) {
        asm_function_t *asm_fn = lower_ir_function(ir_fn);
        LIST_APPEND(&program->fns, asm_fn);
    }

    return program;
}

/*
 * Phase 2: Replace pseudo variables with stack slots
 */

static void
replace_pseudo(asm_operand_t *oper)
{
    if (oper->kind != OPERAND_PSEUDO)
        return;

    int id = oper->pseudo;

    // 0 means empty slot
    // First use: take the next slot aligned to pseudos own size
    if (x86.pseudo_offset[id] == 0) {
        int size = x86.pseudo_size[id];

        x86.frame_size += size;
        x86.frame_size = (x86.frame_size + size - 1) / size * size;
        x86.pseudo_offset[id] = -x86.frame_size;
    }

    *oper = operand_stack(x86.pseudo_offset[id]);
}

static void
assign_stack_slots(asm_function_t *fn)
{
    x86.frame_size = 0;

    LIST_FOREACH(instr, &fn->instrs) {
        switch (instr->kind) {
            case ASM_INSTR_MOV:
            case ASM_INSTR_MOVSX:
                replace_pseudo(&instr->mov.src);
                replace_pseudo(&instr->mov.dst);
                break;
            case ASM_INSTR_UNARY:
                replace_pseudo(&instr->unary.dst);
                break;
            case ASM_INSTR_BINARY:
                replace_pseudo(&instr->binary.src);
                replace_pseudo(&instr->binary.dst);
                break;
            case ASM_INSTR_CMP:
                replace_pseudo(&instr->cmp.src);
                replace_pseudo(&instr->cmp.dst);
                break;
            case ASM_INSTR_IDIV:
                replace_pseudo(&instr->idiv.divisor);
                break;
            case ASM_INSTR_SETCC:
                replace_pseudo(&instr->setcc.dst);
                break;
            case ASM_INSTR_PUSH:
                replace_pseudo(&instr->push.src);
                break;
            default:
                break;
        }
    }

    // %rsp must be aligned to 16 bytes
    fn->stack_size = (x86.frame_size + 15) / 16 * 16;
}

/*
 * Phase 3: Replace illegal operator-operator combos
 */

bool 
is_memory_operand(asm_operand_t oper)
{
    return oper.kind == OPERAND_STACK ||
        oper.kind == OPERAND_DATA;
}

bool
is_large_imm(asm_operand_t oper)
{
    return oper.kind == OPERAND_IMM &&
        (oper.imm > INT32_MAX || oper.imm < INT32_MIN);
}

static void
fixup_mov(asm_instr_t *instr)
{
    asm_size size = instr->size;
    asm_operand_t src = instr->mov.src;
    asm_operand_t dst = instr->mov.dst;
    asm_operand_t r10 = operand_reg(REG_R10);

    if (size == ASM_LONGWORD && src.kind == OPERAND_IMM)
        src.imm = (int32_t)src.imm;

    // No memory-to-memory, or 64bit imm to memory
    if ((is_memory_operand(src) && is_memory_operand(dst)) ||
            is_large_imm(src)) {
        emit_mov(size, src, r10);
        emit_mov(size, r10, dst);
        return;
    }

    emit_mov(size, src, dst);
}

static void
fixup_movsx(asm_instr_t *instr)
{
    asm_operand_t src = instr->mov.src;
    asm_operand_t dst = instr->mov.dst;
    asm_operand_t r10 = operand_reg(REG_R10);
    asm_operand_t r11 = operand_reg(REG_R11);

    // Source can't be immediate
    if (src.kind == OPERAND_IMM) {
        emit_mov(ASM_LONGWORD, src, r10);
        src = r10;
    }

    // Destination must be register
    if (is_memory_operand(dst)) {
        emit_movsx(src, r11);
        emit_mov(ASM_QUADWORD, r11, dst);
        return;
    }

    emit_movsx(src, dst);
}

static void
fixup_binary(asm_instr_t *instr)
{
    asm_size size = instr->size;
    asm_binary_op op = instr->binary.op;
    asm_operand_t src = instr->binary.src;
    asm_operand_t dst = instr->binary.dst;
    asm_operand_t r10 = operand_reg(REG_R10);
    asm_operand_t r11 = operand_reg(REG_R11);

    // No instruction takes two memory operands
    if (is_memory_operand(src) && is_memory_operand(dst)) {
        emit_mov(size, src, r10);
        src = r10;
    }

    // An immediate that doesn't fit 32 bits must be in register
    if (is_large_imm(src)) {
        emit_mov(size, src, r10);
        src = r10;
    }

    // imul can't write to memory
    if (op == ASM_BINARY_IMUL && is_memory_operand(dst)) {
        emit_mov(size, dst, r11);
        emit_binary(size, op, src, r11);
        emit_mov(size, r11, dst);
        return;
    }

    emit_binary(size, op, src, dst);
}

static void
fixup_idiv(asm_instr_t *instr)
{
    asm_size size = instr->size;
    asm_operand_t divisor = instr->idiv.divisor;
    
    // Can't idiv $imm
    if (divisor.kind == OPERAND_IMM) {
        emit_mov(instr->size, instr->idiv.divisor, operand_reg(REG_R10));
        divisor = operand_reg(REG_R10);
    }

    emit_idiv(size, divisor);
}

static void
fixup_cmp(asm_instr_t *instr)
{
    asm_size size = instr->size;
    asm_operand_t src = instr->cmp.src;
    asm_operand_t dst = instr->cmp.dst;
    asm_operand_t r10 = operand_reg(REG_R10);
    asm_operand_t r11 = operand_reg(REG_R11);

    if ((is_memory_operand(src) && is_memory_operand(dst)) ||
            is_large_imm(src)) {
        emit_mov(size, src, r10);
        src = r10;
    }

    // Second operand can't be immediate
    if (dst.kind == OPERAND_IMM) {
        emit_mov(size, dst, r11);
        dst = r11;
    }

    emit_cmp(size, src, dst);
}

static void
fixup_push(asm_instr_t *instr)
{
    asm_operand_t src = instr->push.src;
    asm_operand_t r10 = operand_reg(REG_R10);

    if (is_large_imm(src)) {
        emit_mov(instr->size, src, operand_reg(REG_R10));
        src = r10;
    }

    emit_push(src);
}

static void
fixup_instr(asm_instr_t *instr)
{
    switch (instr->kind) {
        case ASM_INSTR_MOV:
            fixup_mov(instr);
            break;
        case ASM_INSTR_MOVSX:
            fixup_movsx(instr);
            break;
        case ASM_INSTR_BINARY:
            fixup_binary(instr);
            break;
        case ASM_INSTR_IDIV:
            fixup_idiv(instr);
            break;
        case ASM_INSTR_CMP:
            fixup_cmp(instr);
            break;
        case ASM_INSTR_PUSH:
            fixup_push(instr);
            break;
        default:
            // Jumps, labels, call, ret, cdq, setcc, unary: always valid
            LIST_APPEND(&x86.fn->instrs, instr);
            break;
    }
}

static void
fixup_function(asm_function_t *fn)
{
    typeof(fn->instrs) old = fn->instrs; // Set the old list aside
    LIST_INIT(&fn->instrs);

    x86.fn = fn;

    for (asm_instr_t *instr = old.head, *next; instr; instr = next) {
        next = instr->next;
        fixup_instr(instr);
    }

    x86.fn = nullptr;
}

/*
 * Phase 4: Write asembly to file
 */

// quadword, longword, word, byte, high byte
static const char *reg_names[][5] = {
#define X(name, q, l, w, b, h) [name] = { q, l, w, b, h },
    ASM_REG_LIST
#undef X
};

static const char *cond_suffixes[] = {
#define X(name, suffix) [name] = suffix,
    ASM_COND_LIST
#undef X
};

static const char *unary_names[] = {
#define X(name, str) [name] = str,
    ASM_UNARY_OP_LIST
#undef X
};

static const char *binary_names[] = {
#define X(name, str) [name] = str,
    ASM_BINARY_OP_LIST
#undef X
};

static const char *
reg_name(asm_reg reg, asm_size size)
{
    switch (size) {
        case ASM_BYTE:
            return reg_names[reg][3];
        case ASM_WORD:
            return reg_names[reg][2];
        case ASM_LONGWORD:
            return reg_names[reg][1];
        case ASM_QUADWORD:
            return reg_names[reg][0];
    }
}

static char
size_suffix(asm_size size)
{
    return size == ASM_QUADWORD ? 'q' : 'l';
}

static void
write_symbol_name(symbol_t *symbol, FILE *out)
{
    fprintf(out, "%.*s", (int)symbol->name.len, symbol->name.start);

    if (symbol->linkage == LINKAGE_NONE)
        fprintf(out, ".%zu", (size_t)symbol->id);
}

static void
write_operand(asm_operand_t oper, asm_size size, FILE *out)
{
    switch (oper.kind) {
        case OPERAND_IMM:
            fprintf(out, "$%ld", oper.imm);
            break;
        case OPERAND_STACK:
            fprintf(out, "%d(%%rbp)", oper.stack);
            break;
        case OPERAND_DATA:
            write_symbol_name(oper.data, out);
            fprintf(out, "(%%rip)");
            break;
        case OPERAND_REG:
            fprintf(out, "%%%s", reg_name(oper.reg, size));
            break;
        case OPERAND_PSEUDO:
            // Already substituted in phase 2
            break;
    }
}

static void
write_label(ir_label_t label, FILE *out)
{
    static const char kinds[] = {
        [IR_LABEL_TEMP] = 't',
        [IR_LABEL_CASE] = 's',
        [IR_LABEL_CONTINUE] = 'c',
        [IR_LABEL_BREAK] = 'b',
        [IR_LABEL_USER] = 'u',
    };

    fprintf(out, ".L%c%u", kinds[label.kind], label.id);
}

static void
write_instr(asm_instr_t *instr, FILE *out)
{
    asm_size size = instr->size;

    switch (instr->kind) {
        case ASM_INSTR_MOV:
            fprintf(out, "\tmov%c ", size_suffix(size));
            write_operand(instr->mov.src, size, out);
            fprintf(out, ", ");
            write_operand(instr->mov.dst, size, out);
            break;
        case ASM_INSTR_MOVSX:
            fprintf(out, "\tmovslq ");
            write_operand(instr->mov.src, ASM_LONGWORD, out);
            fprintf(out, ", ");
            write_operand(instr->mov.dst, ASM_QUADWORD, out);
            break;
        case ASM_INSTR_UNARY:
            fprintf(out, "\t%s%c ",
                    unary_names[instr->unary.op],
                    size_suffix(instr->size));
            write_operand(instr->unary.dst, size, out);
            break;
        case ASM_INSTR_BINARY: {
            asm_binary_op op = instr->binary.op;
            asm_operand_t src = instr->binary.src;

            bool count_in_reg =
                (op == ASM_BINARY_SAL || op == ASM_BINARY_SAR) &&
                src.kind == OPERAND_REG;
            
            fprintf(out, "\t%s%c ", binary_names[op], size_suffix(size));
            write_operand(src, count_in_reg ? ASM_BYTE : size, out);
            fprintf(out, ", ");
            write_operand(instr->binary.dst, size, out);
            break;
        }
        case ASM_INSTR_CMP:
            fprintf(out, "\tcmp%c ", size_suffix(size));
            write_operand(instr->cmp.src, size, out);
            fprintf(out, ", ");
            write_operand(instr->cmp.dst, size, out);
            break;
        case ASM_INSTR_IDIV:
            fprintf(out, "\tidiv%c ", size_suffix(size));
            write_operand(instr->idiv.divisor, size, out);
            break;
        case ASM_INSTR_CDQ:
            fprintf(out, size == ASM_QUADWORD ? "\tcqo" : "\tcdq");
            break;
        case ASM_INSTR_JMP:
            fprintf(out, "\tjmp ");
            write_label(instr->jmp.target, out);
            break;
        case ASM_INSTR_JMPCC:
            fprintf(out, "\tj%s ", cond_suffixes[instr->jmpcc.cond]);
            write_label(instr->jmpcc.target, out);
            break;
        case ASM_INSTR_SETCC:
            fprintf(out, "\tset%s ", cond_suffixes[instr->setcc.cond]);
            write_operand(instr->setcc.dst, size, out);
            break;
        case ASM_INSTR_LABEL:
            write_label(instr->label.label, out);
            fprintf(out, ":");
            break;
        case ASM_INSTR_PUSH:
            fprintf(out, "\tpushq ");
            write_operand(instr->push.src, size, out);
            break;
        case ASM_INSTR_CALL:
            fprintf(out, "\tcall ");
            write_symbol_name(instr->call.callee, out);

            if (!instr->call.callee->defined)
                fprintf(out, "@PLT");
            break;
        case ASM_INSTR_RET:
            fprintf(out, "\tmovq %%rbp, %%rsp\n");
            fprintf(out, "\tpopq %%rbp\n");
            fprintf(out, "\tret");
    }

    fprintf(out, "\n");
}

static void
write_function(asm_function_t *fn, FILE *out)
{
    if (fn->sym->linkage == LINKAGE_EXTERNAL) {
        fprintf(out, "\t.globl ");
        write_symbol_name(fn->sym, out);
        fprintf(out, "\n");
    }

    fprintf(out, "\t.text\n");
    write_symbol_name(fn->sym, out);
    fprintf(out, ":\n");

    // Function prologue
    fprintf(out, "\tpushq %%rbp\n");
    fprintf(out, "\tmovq %%rsp, %%rbp\n");
    if (fn->stack_size)
        fprintf(out, "\tsubq $%d, %%rsp\n", fn->stack_size);

    LIST_FOREACH(instr, &fn->instrs) {
        write_instr(instr, out);
    }
}

static void
write_static(symbol_t *sym, FILE *out)
{
    if (sym->kind != SYMBOL_OBJECT || sym->init != INIT_CONSTANT)
        return;

    size_t size = type_size(sym->ty);

    if (sym->linkage == LINKAGE_EXTERNAL) {
        fprintf(out, "\t.globl ");
        write_symbol_name(sym, out);
        fprintf(out, "\n");
    }

    fprintf(out, "\t%s\n", sym->init_value == 0 ? ".bss" : ".data");
    fprintf(out, "\t.align %zu\n", size);

    write_symbol_name(sym, out);
    fprintf(out, ":\n");

    if (sym->init_value == 0)
        fprintf(out, "\t.zero %zu\n", size);
    else if (size == 4)
        fprintf(out, "\t.long %ld\n", (long)sym->init_value);
    else if (size == 8)
        fprintf(out, "\t.quad %ld\n", (long)sym->init_value);
}

bool
asm_emit(
        ir_program_t *ir_program,
        sema_result_t *sema,
        FILE *out)
{
    // Phase 1
    asm_program_t *program = lower_ir_program(ir_program);

    // Phase 2
    LIST_FOREACH(fn, &program->fns) {
        assign_stack_slots(fn);
    }

    // Phase 3
    LIST_FOREACH(fn, &program->fns) {
        fixup_function(fn);
    }

    // Phase 4
    LIST_FOREACH(sym, &sema->symbols) {
        write_static(sym, out);
    }

    LIST_FOREACH(fn, &program->fns) {
        write_function(fn, out);
    }

    fprintf(out, "\t.section .note.GNU-stack,\"\",@progbits\n");

    return true;
}
