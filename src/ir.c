#include "ir.h"
#include "ast.h"
#include "base/vector.h"
#include "lexer.h"
#include "type.h"
#include "base/list.h"
#include "base/memory.h"

typedef struct {
    ir_program_t *program;
    ir_function_t *fn;
    uint32_t next_pseudo_id;
    uint32_t next_label_id;
} ir_builder_t;

static ir_builder_t ir;

static ir_value_t
value_none(void)
{
    return (ir_value_t){ .kind = IR_VALUE_NONE };
}

static ir_value_t
value_constant(int64_t value, type_t *ty)
{
    return (ir_value_t){
        .kind = IR_VALUE_CONSTANT,
        .ty = ty,
        .constant = value
    };
}

static ir_value_t
value_temp(type_t *ty)
{
    return (ir_value_t){
        .kind = IR_VALUE_PSEUDO,
        .ty = ty,
        .pseudo = ir.next_pseudo_id++
    };
}

static ir_value_t
value_object(type_t *ty, symbol_t *sym)
{
    if (sym->sd == STORAGE_DURATION_STATIC) {
        return (ir_value_t){
            .kind = IR_VALUE_STATIC,
            .ty = ty,
            .sym = sym
        };
    } else {
        return (ir_value_t){
            .kind = IR_VALUE_PSEUDO,
            .ty = ty,
            .pseudo = (uint32_t)sym->id
        };
    }
}

static ir_label_t
make_label(ir_label_kind kind, uint32_t id)
{
    return (ir_label_t){
        .kind = kind,
        .id = id
    };
}

static ir_label_t
temp_label(void)
{
    return make_label(IR_LABEL_TEMP, ir.next_label_id++);
}

static ir_unary_op
convert_unary_op(token_kind kind)
{
    switch (kind) {
        case TOKEN_MINUS:
            return IR_UNARY_NEG;
        case TOKEN_TILDE:
            return IR_UNARY_BIT_NOT;
        case TOKEN_BANG:
            return IR_UNARY_LOG_NOT;
        default:
            (void)0;
    }
}

static ir_binary_op
convert_binary_op(token_kind kind)
{
    switch (kind) {
        case TOKEN_PLUS:
        case TOKEN_PLUS_EQUAL:
            return IR_BINARY_ADD;
        case TOKEN_MINUS:
        case TOKEN_MINUS_EQUAL:
            return IR_BINARY_SUB;
        case TOKEN_STAR:
        case TOKEN_STAR_EQUAL:
            return IR_BINARY_MUL;
        case TOKEN_SLASH:
        case TOKEN_SLASH_EQUAL:
            return IR_BINARY_DIV;
        case TOKEN_PERCENT:
        case TOKEN_PERCENT_EQUAL:
            return IR_BINARY_REM;
        case TOKEN_AND:
        case TOKEN_AND_EQUAL:
            return IR_BINARY_BIT_AND;
        case TOKEN_OR:
        case TOKEN_OR_EQUAL:
            return IR_BINARY_BIT_OR;
        case TOKEN_CARET:
        case TOKEN_CARET_EQUAL:
            return IR_BINARY_BIT_XOR;
        case TOKEN_LESS_LESS:
        case TOKEN_LESS_LESS_EQUAL:
            return IR_BINARY_SHL;
        case TOKEN_GREATER_GREATER:
        case TOKEN_GREATER_GREATER_EQUAL:
            return IR_BINARY_SHR;
        case TOKEN_EQUAL_EQUAL:
            return IR_BINARY_EQ;
        case TOKEN_BANG_EQUAL:
            return IR_BINARY_NE;
        case TOKEN_LESS:
            return IR_BINARY_LT;
        case TOKEN_LESS_EQUAL:
            return IR_BINARY_LE;
        case TOKEN_GREATER:
            return IR_BINARY_GT;
        case TOKEN_GREATER_EQUAL:
            return IR_BINARY_GE;
        default:
            (void)0;
    }
}

static ir_instr_t *
instr_new(ir_instr_kind kind)
{
    ir_instr_t *instr = xcalloc(1, sizeof(ir_instr_t));
    instr->kind = kind;

    LIST_APPEND(&ir.fn->instrs, instr);
    return instr;
}

static void
emit_return(ir_value_t src)
{
    ir_instr_t *instr = instr_new(IR_INSTR_RETURN);
    instr->ret.src = src;
}

static void
emit_unary(ir_unary_op op, ir_value_t src, ir_value_t dst)
{
    ir_instr_t *instr = instr_new(IR_INSTR_UNARY);
    instr->unary.op = op;
    instr->unary.src = src;
    instr->unary.dst = dst;
}

static void
emit_binary(
        ir_binary_op op,
        ir_value_t lhs,
        ir_value_t rhs,
        ir_value_t dst)
{
    ir_instr_t *instr = instr_new(IR_INSTR_BINARY);
    instr->binary.op = op;
    instr->binary.lhs = lhs;
    instr->binary.rhs = rhs;
    instr->binary.dst = dst;
}

static void
emit_copy(ir_value_t src, ir_value_t dst)
{
    ir_instr_t *instr = instr_new(IR_INSTR_COPY);
    instr->copy.src = src;
    instr->copy.dst = dst;
}

static void
emit_jump(ir_label_t target)
{
    ir_instr_t *instr = instr_new(IR_INSTR_JUMP);
    instr->jump.target = target;
}

static void
emit_jump_if_zero(ir_value_t cond, ir_label_t target)
{
    ir_instr_t *instr = instr_new(IR_INSTR_JUMP_IF_ZERO);
    instr->jump_cond.cond = cond;
    instr->jump_cond.target = target;
}

static void
emit_jump_if_not_zero(ir_value_t cond, ir_label_t target)
{
    ir_instr_t *instr = instr_new(IR_INSTR_JUMP_IF_NOT_ZERO);
    instr->jump_cond.cond = cond;
    instr->jump_cond.target = target;
}

static void
emit_label(ir_label_t label)
{
    ir_instr_t *instr = instr_new(IR_INSTR_LABEL);
    instr->label.label = label;
}

static void
emit_call(symbol_t *callee, vector args, ir_value_t dst)
{
    ir_instr_t *instr = instr_new(IR_INSTR_CALL);
    instr->call.callee = callee;
    instr->call.args = args;
    instr->call.dst = dst;
}

static void
emit_cast(ir_value_t src, ir_value_t dst)
{
    ir_instr_t *instr = instr_new(IR_INSTR_CAST);
    instr->cast.src = src;
    instr->cast.dst = dst;
}

/* Returns value converted to ty, emitting cast only when needed */
static ir_value_t
emit_convert(ir_value_t value, type_t *ty)
{
    if (type_compatible(value.ty, ty))
        return value;

    // Constant converted here instead of runtime
    if (value.kind == IR_VALUE_CONSTANT) {
        int64_t converted = type_is_int(ty)
            ? (int32_t)value.constant
            : value.constant;

        return value_constant(converted, ty);
    }

    ir_value_t dst = value_temp(ty);
    emit_cast(value, dst);
    return dst;
}

static ir_value_t
emit_expr(ast_expr_t *expr)
{
    switch (expr->kind) {
        case EXPR_INT_CONSTANT:
        case EXPR_LONG_CONSTANT:
            return value_constant(expr->constant_value, expr->ty);
        case EXPR_IDENTIFIER:
            return value_object(expr->ty, expr->identifier.sym);
        case EXPR_UNARY:
            ir_value_t src = emit_expr(expr->unary.operand);

            if (expr->unary.op.kind == TOKEN_PLUS)
                return src;

            ir_value_t dst = value_temp(expr->ty);
            emit_unary(convert_unary_op(expr->unary.op.kind), src, dst);
            return dst;
        case EXPR_BINARY: {
            token_kind op = expr->binary.op.kind;

            if (op == TOKEN_AND_AND) {
                // a && b:
                //  if a == 0 jump false
                //  if b == 0 jump false
                //  dst = 1; jump end
                // false:
                //  dst = 0
                // end
                ir_label_t false_label = temp_label();
                ir_label_t end_label = temp_label();
                ir_value_t dst = value_temp(expr->ty);

                ir_value_t lhs = emit_expr(expr->binary.left);
                emit_jump_if_zero(lhs, false_label);

                ir_value_t rhs = emit_expr(expr->binary.right);
                emit_jump_if_zero(rhs, false_label);

                emit_copy(value_constant(1, type_int()), dst);
                emit_jump(end_label);

                emit_label(false_label);
                emit_copy(value_constant(0, type_int()), dst);

                emit_label(end_label);
                return dst;
            }

            if (op == TOKEN_OR_OR) {
                // a || b:
                //  if a != 0 jump true
                //  if b != 0 jump true
                //  dst = 0; jump end
                // true:
                //  dst = 1
                // end
                ir_label_t true_label = temp_label();
                ir_label_t end_label = temp_label();
                ir_value_t dst = value_temp(expr->ty);

                ir_value_t lhs = emit_expr(expr->binary.left);
                emit_jump_if_not_zero(lhs, true_label);

                ir_value_t rhs = emit_expr(expr->binary.right);
                emit_jump_if_not_zero(rhs, true_label);

                emit_copy(value_constant(0, expr->ty), dst);
                emit_jump(end_label);

                emit_label(true_label);
                emit_copy(value_constant(1, expr->ty), dst);

                emit_label(end_label);
                return dst;
            }

            ir_value_t lhs = emit_expr(expr->binary.left);
            ir_value_t rhs = emit_expr(expr->binary.right);
            ir_value_t dst = value_temp(expr->ty);

            emit_binary(convert_binary_op(op), lhs, rhs, dst);
            return dst;
        }
        case EXPR_ASSIGNMENT: {
            ir_value_t lhs = value_object(expr->ty, expr->assignment.lvalue->identifier.sym);
            ir_value_t rhs = emit_expr(expr->assignment.rvalue);

            // Normal assignment
            if (expr->tok.kind == TOKEN_EQUAL) {
                emit_copy(rhs, lhs);
                return lhs;
            }

            // a op= b:
            //  left = a converted op_ty
            //  right = left op b
            //  a = result converted back to type of a
            type_t *op_ty = expr->assignment.op_ty;
            ir_value_t left = emit_convert(lhs, op_ty);
            ir_value_t result = value_temp(op_ty);

            emit_binary(convert_binary_op(expr->assignment.op.kind), left, rhs, result);
            emit_copy(emit_convert(result, lhs.ty), lhs);
            return lhs;
        }
        case EXPR_PRE:
        case EXPR_POST:
            ir_binary_op op = expr->unary.op.kind == TOKEN_PLUS_PLUS ? IR_BINARY_ADD : IR_BINARY_SUB;

            ir_value_t lhs = emit_expr(expr->unary.operand);
            ir_value_t one = value_constant(1, lhs.ty);

            if (expr->kind == EXPR_PRE) {
                emit_binary(op, lhs, one, lhs);
                return lhs;
            }

            // post yields value pre incr/decr
            ir_value_t old_lhs = value_temp(lhs.ty);
            emit_copy(lhs, old_lhs);
            emit_binary(op, lhs, one, lhs);
            return old_lhs;
        case EXPR_CONDITIONAL: {
            // c ? a : b:
            //   if c == 0 jump else
            //   dst = a; jump end
            // else:
            //   dst = b
            // end:
            ir_label_t else_label = temp_label();
            ir_label_t end_label = temp_label();

            // Both branches are void calls: there is no value to copy
            bool has_value = !type_is_void(expr->ty);
            ir_value_t dst = has_value ? value_temp(expr->ty) : value_none();

            ir_value_t cond = emit_expr(expr->conditional.condition);
            emit_jump_if_zero(cond, else_label);

            ir_value_t then_value = emit_expr(expr->conditional.then_expr);
            if (has_value)
                emit_copy(then_value, dst);
            emit_jump(end_label);

            emit_label(else_label);

            ir_value_t else_value = emit_expr(expr->conditional.else_expr);
            if (has_value)
                emit_copy(else_value, dst);

            emit_label(end_label);
            return dst;
        }
        case EXPR_CALL: {
            symbol_t *calle = expr->call.callee->identifier.sym;

            vector args;
            VECTOR_INIT(&args, ir_value_t);

            LIST_FOREACH(arg, &expr->call.args) {
                ir_value_t value = emit_expr(arg);
                vector_push(&args, &value);
            }

            ir_value_t dst = type_is_void(expr->ty)
                ? value_none()
                : value_temp(expr->ty);

            emit_call(calle, args, dst);
            return dst;
        }
        case EXPR_CAST: {
            ir_value_t src = emit_expr(expr->cast.operand);

            // (void)x: evaluate x for its side effects
            if (type_is_void(expr->ty))
                return value_none();

            return emit_convert(src, expr->ty);
        }
    }
}

static void
emit_declaration_list(ast_stmt_t *stmt)
{
    if (!stmt)
        return;

    LIST_FOREACH(decl, &stmt->decl.decls) {
        if (decl->kind != DECL_OBJECT)
            continue;

        if (decl->sd != STORAGE_DURATION_AUTO)
            continue;

        if (decl->object.init) {
            ir_value_t dst = value_object(decl->ty, decl->sym);
            ir_value_t src = emit_expr(decl->object.init);

            emit_copy(src, dst);
        }
    }
}

static void
emit_stmt(ast_stmt_t *stmt)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_NULL:
            break;
        case STMT_EXPR:
            emit_expr(stmt->expr.expr);
            break;
        case STMT_DECL:
            emit_declaration_list(stmt);
            break;
        case STMT_BLOCK:
            LIST_FOREACH(item, &stmt->block.items) {
                emit_stmt(item);
            }
            break;
        case STMT_RETURN:
            if (stmt->return_stmt.expr)
                emit_return(emit_expr(stmt->return_stmt.expr));
            else
                emit_return(value_none());
            break;
        case STMT_IF:
            ir_value_t cond = emit_expr(stmt->if_stmt.condition);

            if (!stmt->if_stmt.else_stmt) {
                ir_label_t end_label =
                    make_label(IR_LABEL_TEMP, ir.next_label_id++);

                emit_jump_if_zero(cond, end_label);
                emit_stmt(stmt->if_stmt.then_stmt);
                emit_label(end_label);
                break;
            }

            ir_label_t end_label = temp_label();
            ir_label_t else_label = temp_label();

            emit_jump_if_zero(cond, else_label);
            emit_stmt(stmt->if_stmt.then_stmt);
            emit_jump(end_label);

            emit_label(else_label);
            emit_stmt(stmt->if_stmt.else_stmt);

            emit_label(end_label);
            break;
        case STMT_FOR: {
            ir_label_t start_label = temp_label();
            ir_label_t break_label =
                make_label(IR_LABEL_BREAK, stmt->id);
            ir_label_t continue_label =
                make_label(IR_LABEL_CONTINUE, stmt->id);

            emit_stmt(stmt->loop.init);

            emit_label(start_label);

            if (stmt->loop.condition) {
                ir_value_t cond = emit_expr(stmt->loop.condition);
                emit_jump_if_zero(cond, break_label);
            }

            emit_stmt(stmt->loop.body);

            emit_label(continue_label);

            if (stmt->loop.post)
                emit_expr(stmt->loop.post);

            emit_jump(start_label);
            emit_label(break_label);
            break;
        }
        case STMT_WHILE: {
            ir_label_t break_label =
                make_label(IR_LABEL_BREAK, stmt->id);
            ir_label_t continue_label =
                make_label(IR_LABEL_CONTINUE, stmt->id);

            emit_label(continue_label);

            ir_value_t cond = emit_expr(stmt->loop.condition);
            emit_jump_if_zero(cond, break_label);

            emit_stmt(stmt->loop.body);

            emit_jump(continue_label);
            emit_label(break_label);
            break;
        }
        case STMT_DOWHILE: {
            ir_label_t start_label = temp_label();
            ir_label_t break_label =
                make_label(IR_LABEL_BREAK, stmt->id);
            ir_label_t continue_label =
                make_label(IR_LABEL_CONTINUE, stmt->id);

            emit_label(start_label);
            emit_stmt(stmt->loop.body);
            emit_label(continue_label);

            ir_value_t cond = emit_expr(stmt->loop.condition);
            emit_jump_if_not_zero(cond, start_label);

            emit_label(break_label);
            break;
        }
        case STMT_SWITCH: {
            ir_label_t break_label =
                make_label(IR_LABEL_BREAK, stmt->id);

            ir_value_t cond = emit_expr(stmt->switch_stmt.condition);

            for (ast_stmt_t *c = stmt->switch_stmt.cases; c; c = c->case_stmt.next_case) {
                ir_value_t value = value_constant(c->case_stmt.value, cond.ty);
                ir_value_t matches = value_temp(type_int());
                emit_binary(IR_BINARY_EQ, cond, value, matches);
                emit_jump_if_not_zero(matches, make_label(IR_LABEL_CASE, c->id));
            }

            ast_stmt_t *default_case = stmt->switch_stmt.default_case;
            if (default_case)
                emit_jump(make_label(IR_LABEL_CASE, default_case->id));
            else
                emit_jump(break_label);

            emit_stmt(stmt->switch_stmt.body);
            emit_label(break_label);
            break;
        }
        case STMT_DEFAULT:
            emit_label(make_label(IR_LABEL_CASE, stmt->id));
            emit_stmt(stmt->default_stmt.stmt);
            break;
        case STMT_CASE:
            emit_label(make_label(IR_LABEL_CASE, stmt->id));
            emit_stmt(stmt->case_stmt.stmt);
            break;
        case STMT_BREAK:
            emit_jump(make_label(IR_LABEL_BREAK, stmt->break_stmt.target->id));
            break;
        case STMT_CONTINUE:
            emit_jump(make_label(IR_LABEL_CONTINUE, stmt->continue_stmt.target->id));
            break;
        case STMT_GOTO:
            emit_jump(make_label(IR_LABEL_USER, stmt->goto_stmt.target->id));
            break;
        case STMT_LABEL:
            emit_label(make_label(IR_LABEL_USER, stmt->id));
            emit_stmt(stmt->label_stmt.stmt);
            break;
    }
}

static void
emit_fallthrough_return(ast_decl_t *decl)
{
    type_t *return_ty = decl->ty->function.return_ty;

    if (type_is_void(return_ty))
        emit_return(value_none());
    else
        emit_return(value_constant(0, return_ty));
}

static void
emit_function(ast_decl_t *decl)
{
    ir_function_t *function = xcalloc(1, sizeof(ir_function_t));
    function->sym = decl->sym;
    VECTOR_INIT(&function->params, ir_value_t);

    LIST_FOREACH(param, &decl->function.params) {
        ir_value_t value = value_object(param->ty, param->sym);
        vector_push(&function->params, &value);
    }

    ir.fn = function;

    emit_stmt(decl->function.body);
    emit_fallthrough_return(decl);

    ir.fn = nullptr;
    
    LIST_APPEND(&ir.program->fns, function);
}

ir_program_t *
ir_build(const sema_result_t *sema)
{
    ir_program_t *program = xcalloc(1, sizeof(ir_program_t));
    program->sema = sema;

    ir = (ir_builder_t){
        .program = program,
        .next_pseudo_id = sema->symbol_count
    };

    LIST_FOREACH(decl, &sema->program->decls) {
        if (decl->kind == DECL_FUNCTION && decl->function.body)
            emit_function(decl);
    }

    return program;
}
