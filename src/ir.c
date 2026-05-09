#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "ir.h"
#include "ast.h"
#include "sema.h"
#include "type.h"
#include "base/hash_map.h"

static struct ir_function *current_function;
extern struct symbol *all_symbols; // From sema (for now simple) TODO: Change this

/*
 * One per function.
 * Maps label string to label int.
 */
static hash_map label_ids;

static int next_temp_id;
static int next_label_id;

static struct ir_value ir_constant(long c) 
{
    return (struct ir_value) {
        .kind = IR_VALUE_CONSTANT,
        .constant = c
    };
}

static struct ir_value ir_name(const char *name)
{
    return (struct ir_value) {
        .kind = IR_VALUE_NAME,
        .name = name
    };
}

static struct ir_value make_temp(void)
{
    int len = snprintf(NULL, 0, "tmp.%d", next_temp_id);
    char *buf = malloc(len + 1);

    snprintf(buf, len + 1, "tmp.%d", next_temp_id++);

    return ir_name(buf);
}

static int make_label(void)
{
    return next_label_id++;
}

static int get_or_create_label_id(const char *name, int len)
{
    void *value = hashmap_get(&label_ids, name, len);
    if (value)
        return (int)(intptr_t)value;

    int id = make_label();
    hashmap_set(&label_ids, name, len, (void *)(intptr_t)id);

    return id;
}

static int get_or_create_label_id_tok(struct token *tok)
{
    return get_or_create_label_id(tok->start, tok->length);
}

static int get_or_create_label_id_cstr(const char *str)
{
    return get_or_create_label_id(str, strlen(str));
}

static enum ir_unary_op convert_unary_op(struct token tok)
{
    switch (tok.type) {
        case TOKEN_MINUS:
            return IR_UNOP_NEG;
        case TOKEN_TILDE:
            return IR_UNOP_BIT_NOT;
        case TOKEN_BANG:
            return IR_UNOP_LOG_NOT;
        default:
            break;
    }
}

static enum ir_binary_op convert_binary_op(struct token tok)
{
    switch (tok.type) {
        case TOKEN_PLUS:            return IR_BINOP_ADD;
        case TOKEN_MINUS:           return IR_BINOP_SUB;
        case TOKEN_STAR:            return IR_BINOP_MUL;
        case TOKEN_SLASH:           return IR_BINOP_DIV;
        case TOKEN_PERCENT:         return IR_BINOP_REM;
        case TOKEN_AND:             return IR_BINOP_BIT_AND;
        case TOKEN_OR:              return IR_BINOP_BIT_OR;
        case TOKEN_CARET:           return IR_BINOP_BIT_XOR;
        case TOKEN_EQUAL_EQUAL:     return IR_BINOP_EQ;
        case TOKEN_BANG_EQUAL:      return IR_BINOP_NE;
        case TOKEN_LESS:            return IR_BINOP_LT;
        case TOKEN_LESS_EQUAL:      return IR_BINOP_LE;
        case TOKEN_GREATER:         return IR_BINOP_GT;
        case TOKEN_GREATER_EQUAL:   return IR_BINOP_GE;
        case TOKEN_GREATER_GREATER: return IR_BINOP_SHR;
        case TOKEN_LESS_LESS:       return IR_BINOP_SHL;

        // Compound assignments
        case TOKEN_PLUS_EQUAL:     return IR_BINOP_ADD;
        case TOKEN_MINUS_EQUAL:    return IR_BINOP_SUB;
        case TOKEN_STAR_EQUAL:     return IR_BINOP_MUL;
        case TOKEN_SLASH_EQUAL:    return IR_BINOP_DIV;
        case TOKEN_PERCENT_EQUAL:  return IR_BINOP_REM;
        case TOKEN_AND_EQUAL:      return IR_BINOP_BIT_AND;
        case TOKEN_OR_EQUAL:       return IR_BINOP_BIT_OR;
        case TOKEN_CARET_EQUAL:    return IR_BINOP_BIT_XOR;
        case TOKEN_LESS_LESS_EQUAL:return IR_BINOP_SHL;
        case TOKEN_GREATER_GREATER_EQUAL:return IR_BINOP_SHR;
        default: break;
    }
}

static void append_instr(struct ir_instr *instr)
{
    if (!current_function->first)
        current_function->first = instr;
    else
        current_function->last->next = instr;
    current_function->last = instr;
}

static void append_function(struct ir_program *program, struct ir_function *fn)
{
    struct ir_function **tail = &program->functions;

    while (*tail)
        tail = &(*tail)->next;

    *tail = fn;
}

static void append_static_variable(struct ir_program *program,
                                struct ir_static_variable *var)
{
    struct ir_static_variable **tail = &program->static_vars;

    while (*tail)
        tail = &(*tail)->next;

    *tail = var;
}

static void append_param(struct ir_function *fn, struct ir_param *param)
{
    struct ir_param **tail = &fn->params;

    while (*tail)
        tail = &(*tail)->next;

    *tail = param;
}

static struct ir_instr *new_instr(enum ir_instr_kind kind)
{
    struct ir_instr *instr = calloc(1, sizeof(struct ir_instr));
    instr->kind = kind;

    return instr;
}

static void emit_return_value(struct ir_value value)
{
    struct ir_instr *instr = new_instr(IR_INSTR_RETURN);
    instr->ret.has_value = true;
    instr->ret.src = value;

    append_instr(instr);
}

static void emit_return_void(void)
{
    struct ir_instr *instr = new_instr(IR_INSTR_RETURN);
    instr->ret.has_value = false;

    append_instr(instr);
}

static void emit_unary(enum ir_unary_op op,
                        struct ir_value src,
                        struct ir_value dst)
{
    struct ir_instr *instr = new_instr(IR_INSTR_UNARY);
    instr->unary.op = op;
    instr->unary.src = src;
    instr->unary.dst = dst;

    append_instr(instr);
}

static void emit_binary(enum ir_binary_op op,
                        struct ir_value lhs,
                        struct ir_value rhs,
                        struct ir_value dst)
{
    struct ir_instr *instr = new_instr(IR_INSTR_BINARY);
    instr->binary.op = op;
    instr->binary.lhs = lhs;
    instr->binary.rhs = rhs;
    instr->binary.dst = dst;

    append_instr(instr);
}

static void emit_copy(struct ir_value src, struct ir_value dst)
{
    struct ir_instr *instr = new_instr(IR_INSTR_COPY);
    instr->copy.src = src;
    instr->copy.dst = dst;

    append_instr(instr);
}

static void emit_jump(int label_id)
{
    struct ir_instr *instr = new_instr(IR_INSTR_JUMP);
    instr->jump.label_id = label_id;

    append_instr(instr);
}

static void emit_jump_if_zero(struct ir_value cond, int label_id)
{
    struct ir_instr *instr = new_instr(IR_INSTR_JUMP_IF_ZERO);
    instr->jump_if_zero.cond = cond;
    instr->jump_if_zero.label_id = label_id;

    append_instr(instr);
}

static void emit_jump_if_not_zero(struct ir_value cond, int label_id)
{
    struct ir_instr *instr = new_instr(IR_INSTR_JUMP_IF_NOT_ZERO);
    instr->jump_if_not_zero.cond = cond;
    instr->jump_if_not_zero.label_id = label_id;

    append_instr(instr);
}

static void emit_label(int label_id)
{
    struct ir_instr *instr = new_instr(IR_INSTR_LABEL);
    instr->label.label_id = label_id;

    append_instr(instr);
}

static void emit_call(const char *calle, struct ir_value *args, int arg_count,
                        bool has_dst, struct ir_value dst)
{
    struct ir_instr *instr = new_instr(IR_INSTR_CALL);
    instr->call.calle = calle;
    instr->call.args = args;
    instr->call.arg_count = arg_count;
    instr->call.has_dst = has_dst;
    instr->call.dst = dst;

    append_instr(instr);
}

static struct ir_value emit_expr(struct expr *expr);
static void emit_stmt(struct stmt *stmt);
static void emit_decl_list(struct decl *decls);
static void emit_block_item(struct block_item *item);

static struct ir_value emit_expr(struct expr *expr)
{
    switch (expr->kind) {
        case EXPR_INT_LITERAL:
            return ir_constant(expr->int_value);

        case EXPR_IDENTIFIER:
            return ir_name(expr->identifier.sym->ir_name);

        case EXPR_UNARY: {
            struct ir_value src = emit_expr(expr->unary.operand);

            // Unary plus doesn't do anything
            if (expr->tok.type == TOKEN_PLUS)
                return src;
            
            struct ir_value dst = make_temp();

            emit_unary(convert_unary_op(expr->tok), src, dst);
            return dst;
        }

        case EXPR_BINARY: {
            // Special cases for && and || (short-circut)
           if (expr->tok.type == TOKEN_AND_AND) {
                // a && b ->
                //  v1 = emit_expr(a); if a == 0 jump false
                //  v2 = emit_expr(b); if b == 0 jump false
                //  dst = 1; jump end
                //  false: dst = 0
                //  end:
                int false_label = make_label();
                int end_label = make_label();
                struct ir_value dst = make_temp();

                struct ir_value lhs = emit_expr(expr->binary.left);
                emit_jump_if_zero(lhs, false_label);

                struct ir_value rhs = emit_expr(expr->binary.right);
                emit_jump_if_zero(rhs, false_label);

                emit_copy(ir_constant(1), dst);
                emit_jump(end_label);

                emit_label(false_label);
                emit_copy(ir_constant(0), dst);

                emit_label(end_label);
                return dst;
            }

            if (expr->tok.type == TOKEN_OR_OR) {
                // a || b ->
                //  v1 = emit_expr(a); if a != 0 jump true
                //  v2 = emit_expr(b); if b != 0 jump true
                //  dst = 0; jump end
                //  true: dst = 1
                //  end:
                int true_label = make_label();
                int end_label = make_label();
                struct ir_value dst = make_temp();

                struct ir_value lhs = emit_expr(expr->binary.left);
                emit_jump_if_not_zero(lhs, true_label);

                struct ir_value rhs = emit_expr(expr->binary.right);
                emit_jump_if_not_zero(rhs, true_label);

                emit_copy(ir_constant(0), dst);
                emit_jump(end_label);

                emit_label(true_label);
                emit_copy(ir_constant(1), dst);

                emit_label(end_label);
                return dst;
            }

            // Standard case for binary operations
            struct ir_value lhs = emit_expr(expr->binary.left);
            struct ir_value rhs = emit_expr(expr->binary.right);
            struct ir_value dst = make_temp();

            emit_binary(convert_binary_op(expr->tok), lhs, rhs, dst);
            return dst;
        }

        case EXPR_ASSIGNMENT: {
            struct ir_value lhs = ir_name(expr->assignment.lvalue->identifier.sym->ir_name);

            if (expr->tok.type == TOKEN_EQUAL) {
                struct ir_value rhs = emit_expr(expr->assignment.rvalue);

                emit_copy(rhs, lhs);
                return lhs;
            }

            // Otherwise compound assignment (+= -= &= ...)
            // lvalue = lvalue op rvalue
            struct ir_value rhs = emit_expr(expr->assignment.rvalue);
            emit_binary(convert_binary_op(expr->tok), lhs, rhs, lhs);
            return lhs;
        }

        case EXPR_PRE:
        case EXPR_POST: {
            bool is_incr = expr->tok.type == TOKEN_PLUS_PLUS;

            struct expr *lhs_expr = expr->unary.operand;
            struct ir_value lhs = ir_name(lhs_expr->identifier.sym->ir_name);

            if (expr->kind == EXPR_POST) {
                struct ir_value old_lhs = make_temp();

                emit_copy(lhs, old_lhs);
                emit_binary(is_incr ? IR_BINOP_ADD : IR_BINOP_SUB,
                            lhs,
                            ir_constant(1),
                            lhs);

                return old_lhs;
            }

            emit_binary(is_incr ? IR_BINOP_ADD : IR_BINOP_SUB,
                        lhs,
                        ir_constant(1),
                        lhs);

            return lhs;
        }

        case EXPR_CONDITIONAL: {
            int else_label = make_label();
            int end_label = make_label();

            struct ir_value dst = make_temp();

            struct ir_value cond = emit_expr(expr->conditional.condition);
            emit_jump_if_zero(cond, else_label);

            struct ir_value then_val = emit_expr(expr->conditional.then_expr);
            emit_copy(then_val, dst);
            emit_jump(end_label);

            emit_label(else_label);

            struct ir_value else_val = emit_expr(expr->conditional.else_expr);
            emit_copy(else_val, dst);

            emit_label(end_label);
            return dst;
        }

        case EXPR_CALL: {
            struct symbol *sym = expr->call.callee->identifier.sym;
            const char *calle = sym->ir_name;

            int arg_count = 0;
            for (struct expr *arg = expr->call.args; arg; arg = arg->next)
                arg_count++;

            struct ir_value *args = NULL;
            if (arg_count > 0)
                args = calloc(arg_count, sizeof(struct ir_value));

            int i = 0;
            for (struct expr *arg = expr->call.args; arg; arg = arg->next)
                args[i++] = emit_expr(arg);

            struct type *ret_ty = expr->call.callee->ty->func.return_type;

            if (type_is_void(ret_ty)) {
                emit_call(calle, args, arg_count, false, ir_constant(0));
                return ir_constant(0); // Dummy value, should not be used
            }

            struct ir_value dst = make_temp();
            
            emit_call(calle, args, arg_count, true, dst);

            return dst;
        }     

        default:
            break;
    }
}

static void emit_decl_list(struct decl *decls)
{
    for (struct decl *decl = decls; decl; decl = decl->next) {
        if (decl->kind != DECL_VAR) {
            continue;
        }

        if (decl->storage_duration != SD_AUTO)
            continue;

        if (decl->var.init) {
            struct ir_value dst = ir_name(decl->sym->ir_name);
            struct ir_value src = emit_expr(decl->var.init);
            
            emit_copy(src, dst);
        }
    }
}

static void emit_stmt(struct stmt *stmt)
{
    if (!stmt)
        return;

    switch (stmt->kind) {
        case STMT_NULL:
            break;

        case STMT_EXPR:
            emit_expr(stmt->expr_stmt.expr);
            break;

        case STMT_RETURN:
            if (stmt->return_stmt.expr)
                emit_return_value(emit_expr(stmt->return_stmt.expr));
            else
                emit_return_void();
            break;

        case STMT_IF: {
            struct ir_value cond = emit_expr(stmt->if_stmt.condition);

            if (!stmt->if_stmt.else_stmt) {
                int end_label = make_label();

                emit_jump_if_zero(cond, end_label);
                emit_stmt(stmt->if_stmt.then_stmt);
                emit_label(end_label);
                break;
            } 

            int end_label = make_label();
            int else_label = make_label();

            emit_jump_if_zero(cond, else_label);
            emit_stmt(stmt->if_stmt.then_stmt);
            emit_jump(end_label);

            emit_label(else_label);
            emit_stmt(stmt->if_stmt.else_stmt);

            emit_label(end_label);
            break;
        }

        case STMT_FOR: {
            int start_label = make_label();
            int break_label = get_or_create_label_id_cstr(stmt->for_stmt.break_label);
            int continue_label = get_or_create_label_id_cstr(stmt->for_stmt.continue_label);

            if (stmt->for_stmt.init) {
                if (stmt->for_stmt.init->is_decl)
                    emit_decl_list(stmt->for_stmt.init->decls);
                else
                    emit_expr(stmt->for_stmt.init->expr);
            }

            emit_label(start_label);

            if (stmt->for_stmt.condition) {
                struct ir_value cond = emit_expr(stmt->for_stmt.condition);

                emit_jump_if_zero(cond, break_label);
            }

            emit_stmt(stmt->for_stmt.body);

            emit_label(continue_label);

            if (stmt->for_stmt.post)
                emit_expr(stmt->for_stmt.post);

            emit_jump(start_label);
            emit_label(break_label);
            break;
        }

        case STMT_WHILE: {
            int break_label = get_or_create_label_id_cstr(stmt->while_stmt.break_label);
            int continue_label = get_or_create_label_id_cstr(stmt->while_stmt.continue_label);

            emit_label(continue_label);

            struct ir_value cond = emit_expr(stmt->while_stmt.condition);
            emit_jump_if_zero(cond, break_label);

            emit_stmt(stmt->while_stmt.body);

            emit_jump(continue_label);
            emit_label(break_label);
            break;
        }

        case STMT_DOWHILE: {
            int start_label = make_label();
            int break_label = get_or_create_label_id_cstr(stmt->dowhile_stmt.break_label);
            int continue_label = get_or_create_label_id_cstr(stmt->dowhile_stmt.continue_label);

            emit_label(start_label);

            emit_stmt(stmt->dowhile_stmt.body);

            emit_label(continue_label);

            struct ir_value cond = emit_expr(stmt->dowhile_stmt.condition);
            emit_jump_if_not_zero(cond, start_label);

            emit_label(break_label);
            break;
        }

        case STMT_SWITCH: {
            int break_label = get_or_create_label_id_cstr(stmt->switch_stmt.break_label);
            
            struct ir_value cond = emit_expr(stmt->switch_stmt.condition);
            struct switch_annotation *ann = stmt->switch_stmt.annotation;
            
            /*
             * Emit dispatch chain:
             *   if cond == case_1 goto case_1_label
             *   if cond == case_2 goto case_2_label ...
             *   goto default_or_break
             */
            for (struct case_entry *entry = ann->cases; entry; entry = entry->next) {
                struct stmt *case_node = entry->node;

                if (case_node->kind == STMT_DEFAULT)
                    continue;

                // TODO: Evaluate at compile time
                int value = case_node->case_stmt.value->int_value;
                struct ir_value case_value = ir_constant(value);
                
                int case_label = get_or_create_label_id_cstr(case_node->case_stmt.label);

                struct ir_value cmp = make_temp();
                emit_binary(IR_BINOP_EQ, cond, case_value, cmp);
                emit_jump_if_not_zero(cmp, case_label);
            }

            if (ann->default_node) {
                int default_label = get_or_create_label_id_cstr(ann->default_node->default_stmt.label);
                emit_jump(default_label);
            } else {
                emit_jump(break_label);
            }

            // The body itself emits cases/defaults or other statements
            emit_stmt(stmt->switch_stmt.body);

            emit_label(break_label);
            break;
        }

        case STMT_DEFAULT: {
            int label_id = get_or_create_label_id_cstr(stmt->default_stmt.label);

            emit_label(label_id);

            for (struct block_item *item = stmt->default_stmt.items; item; item = item->next)
                emit_block_item(item);
            break;
        }

        case STMT_CASE: {
            int label_id = get_or_create_label_id_cstr(stmt->case_stmt.label);

            emit_label(label_id);

            for (struct block_item *item = stmt->case_stmt.items; item; item = item->next)
                emit_block_item(item);
            break;
        }
        case STMT_BREAK:
            emit_jump(get_or_create_label_id_cstr(stmt->break_stmt.target_label));
            break;
        case STMT_CONTINUE:
            emit_jump(get_or_create_label_id_cstr(stmt->continue_stmt.target_label));
            break;

        case STMT_GOTO:
            emit_jump(get_or_create_label_id_tok(&stmt->goto_stmt.label));
            break;

        case STMT_LABEL: {
            int label_id = get_or_create_label_id_tok(&stmt->label_stmt.name);

            emit_label(label_id);
            emit_stmt(stmt->label_stmt.stmt);
            break;
        }

        case STMT_BLOCK:
            for (struct block_item *item = stmt->block.items; item; item = item->next)
                emit_block_item(item);
            break;
    }
}

static void emit_block_item(struct block_item *item)
{
    if (!item)
        return;

    if (item->kind == BLOCK_ITEM_DECL)
        emit_decl_list(item->decls);
    else
        emit_stmt(item->stmt);
}

static void emit_static_variables(struct ir_program *ir)
{
    for (struct symbol *sym = all_symbols; sym; sym = sym->next) {
        if (sym->kind != SYM_OBJECT)
            continue;

        if (sym->storage_duration != SD_STATIC)
            continue;

        if (!sym->defined && !sym->tentative)
            continue;

        struct ir_static_variable *var = calloc(1, sizeof(struct ir_static_variable));
        var->name = sym->ir_name;
        var->linkage = sym->linkage;
        var->init = sym->has_static_init ? sym->static_init : 0;

        append_static_variable(ir, var);
    }
}

static void emit_function_params(struct ir_function *fn, struct decl *params)
{
    for (struct decl *param = params; param; param = param->next) {
        struct ir_param *ir_param = calloc(1, sizeof(struct ir_param));
        ir_param->name = param->ir_name;

        append_param(fn, ir_param);
    }
}

/*
 * Fallthrough of main means return 0.
 * Falltrhough of non-void functions is undefined behaviour.
 * For now emit 0 for non-void
 */
static void emit_implicit_fallthrough_return(struct decl *fn_decl)
{
    struct type *ret_ty = fn_decl->ty->func.return_type;

    // TODO: Add some condition to not emit return if we can

    if (type_is_void(ret_ty))
        emit_return_void();
    else
        emit_return_value(ir_constant(0));
}

static struct ir_function *emit_function(struct decl *decl)
{
    struct ir_function *fn = calloc(1, sizeof(struct ir_function));
    fn->name = decl->ir_name;
    fn->linkage = decl->linkage;

    emit_function_params(fn, decl->func.params);

    current_function = fn;

    hashmap_init(&label_ids);

    emit_stmt(decl->func.body);

    emit_implicit_fallthrough_return(decl);

    hashmap_free(&label_ids);

    current_function = NULL;

    return fn;
}

struct ir_program *build_ir(struct ast_program *program)
{
    struct ir_program *ir = calloc(1, sizeof(struct ir_program));

    current_function = NULL;
    next_temp_id = 0;
    next_label_id = 1;

    for (struct decl *decl = program->decls; decl; decl = decl->next) {
        if (decl->kind == DECL_VAR) {
            continue;
        }

        if (decl->kind == DECL_FUNCTION && decl->func.body) {
            struct ir_function *fn = emit_function(decl);
            append_function(ir, fn);
        }
    }

    emit_static_variables(ir);

    return ir;
}
