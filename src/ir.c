#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "ir.h"
#include "ast.h"
#include "lexer.h"
#include "sema.h"
#include "base/hash_map.h"

struct loop_switch_labels {
    int break_label;
    int continue_label;
};

static hash_map goto_labels; // goto label name -> int label_id
static hash_map loop_switch_labels_map; // sema loop / struct label (e.g. "for.3") -> struct loop_labels{ break_id, continue_id }

static struct ir_val make_constant(long c) 
{
    return (struct ir_val){ .type = IR_VAL_CONSTANT, .constant = c };
}

static struct ir_val make_temp(void)
{
    static int id = 0;
    int n = snprintf(NULL, 0, "tmp.%d", id);
    char *buf = malloc(n + 1);
    snprintf(buf, n + 1, "tmp.%d", id++);
    return (struct ir_val){ .type = IR_VAL_VAR, .var.name = buf, .var.length = n };
}

static struct ir_val make_var(struct token *tok)
{
    const char *name = tok->resolved ? tok->resolved : tok->start;
    int length = tok->resolved ? strlen(tok->resolved) : tok->length;
    return (struct ir_val){ .type = IR_VAL_VAR, .var.name = name, .var.length = length };
}

static int make_label(void)
{
    static int label_id = 1;
    return label_id++;
}

static int get_or_create_label_id(const char *name, int len)
{
    void *value = hashmap_get(&goto_labels, name, len);
    if (value)
        return (int)(intptr_t)value;

    int id = make_label();
    hashmap_set(&goto_labels, name, len, (void *)(intptr_t)id);
    return id;
}

static void register_loop_switch(const char *label, int break_id, int continue_id)
{
    struct loop_switch_labels *lbl = malloc(sizeof(*lbl));
    *lbl = (struct loop_switch_labels){ .break_label = break_id, .continue_label = continue_id};
    hashmap_set(&loop_switch_labels_map, label, strlen(label), (void *)lbl);
}

static enum ir_unary_op convert_unop(struct token tok)
{
    switch (tok.type) {
        case TOKEN_MINUS: return IR_NEGATE;
        case TOKEN_TILDE: return IR_COMPLEMENT;
        case TOKEN_BANG:  return IR_NOT;
        default: fprintf(stderr, "unknown unop\n"); exit(1);
    }
}

static enum ir_binary_op convert_binop(struct token tok)
{
    switch (tok.type) {
        case TOKEN_PLUS:            return IR_ADD;
        case TOKEN_MINUS:           return IR_SUBTRACT;
        case TOKEN_STAR:            return IR_MULTIPLY;
        case TOKEN_SLASH:           return IR_DIVIDE;
        case TOKEN_PERCENT:         return IR_REMAINDER;
        case TOKEN_CARET:           return IR_XOR;
        case TOKEN_OR:              return IR_OR;
        case TOKEN_AND:             return IR_AND;
        case TOKEN_EQUAL_EQUAL:     return IR_EQUAL;
        case TOKEN_BANG_EQUAL:      return IR_NOT_EQUAL;
        case TOKEN_LESS:            return IR_LESS;
        case TOKEN_LESS_EQUAL:      return IR_LESS_EQUAL;
        case TOKEN_LESS_LESS:       return IR_SHIFT_LEFT;
        case TOKEN_GREATER:         return IR_GREATER;
        case TOKEN_GREATER_EQUAL:   return IR_GREATER_EQUAL;
        case TOKEN_GREATER_GREATER: return IR_SHIFT_RIGHT;

        // Compound assignments
        case TOKEN_PLUS_EQUAL:     return IR_ADD;
        case TOKEN_MINUS_EQUAL:    return IR_SUBTRACT;
        case TOKEN_STAR_EQUAL:     return IR_MULTIPLY;
        case TOKEN_SLASH_EQUAL:    return IR_DIVIDE;
        case TOKEN_PERCENT_EQUAL:  return IR_REMAINDER;
        case TOKEN_CARET_EQUAL:    return IR_XOR;
        case TOKEN_OR_EQUAL:       return IR_OR;
        case TOKEN_AND_EQUAL:      return IR_AND;
        case TOKEN_LESS_LESS_EQUAL:return IR_SHIFT_LEFT;
        case TOKEN_GREATER_GREATER_EQUAL:return IR_SHIFT_RIGHT;
        default: fprintf(stderr, "unknown binop\n"); exit(1);
    }
}

static void append_instr(struct ir_function *fn, struct ir_instr *instr)
{
    if (!fn->first)
        fn->first = instr;
    else
        fn->last->next = instr;
    fn->last = instr;
}

static struct ir_instr *alloc_instr(void)
{
    struct ir_instr *i = calloc(1, sizeof(struct ir_instr));
    return i;
}

static void emit_return(struct ir_function *fn, struct ir_val src)
{
    struct ir_instr *i = alloc_instr();
    *i = (struct ir_instr){ .type = IR_RETURN, .ret.src = src };
    append_instr(fn, i);
}

static void emit_binary(struct ir_function *fn, enum ir_binary_op op,
        struct ir_val src1, struct ir_val src2, struct ir_val dst)
{
    struct ir_instr *i = alloc_instr();
    *i = (struct ir_instr){
        .type = IR_BINARY,
        .binary.op = op,
        .binary.src1 = src1,
        .binary.src2 = src2,
        .binary.dst = dst
    };
    append_instr(fn, i);
}

static void emit_copy(struct ir_function *fn, struct ir_val src, struct ir_val dst)
{
    struct ir_instr *i = alloc_instr();
    *i = (struct ir_instr){ .type = IR_COPY, .copy.src = src, .copy.dst = dst };
    append_instr(fn, i);
}

static void emit_jump(struct ir_function *fn, int label_id)
{
    struct ir_instr *i = alloc_instr();
    *i = (struct ir_instr){ .type = IR_JUMP, .jump.label_id = label_id };
    append_instr(fn, i);
}

static void emit_jump_cond(struct ir_function *fn, struct ir_val cond,
        int label_id, enum ir_instr_type type)
{
    struct ir_instr *i = alloc_instr();
    if (type == IR_JUMP_IF_ZERO)
        *i = (struct ir_instr){ .type = type, .jump_if_zero.cond = cond, .jump_if_zero.label_id = label_id};
    else
        *i = (struct ir_instr){ .type = type, .jump_if_not_zero.cond = cond, .jump_if_not_zero.label_id = label_id};
    append_instr(fn, i);
}

static void emit_label(struct ir_function *fn, int label_id)
{
    struct ir_instr *i = alloc_instr();
    *i = (struct ir_instr){ .type = IR_LABEL, .label.label_id = label_id };
    append_instr(fn, i);
}

static struct ir_val emit_expr(struct ast_node *expr, struct ir_function *fn)
{
    switch (expr->type) {
        case AST_IDENTIFIER:
            // Variable reference -> IR var using sema resolved unique name
            return make_var(&expr->token);
        case AST_CONSTANT:
            // Integer literal -> IR constant
            return make_constant(expr->constant.value);
        case AST_UNARY: {
            // Unary op -> tmp = unary_op src
            struct ir_val src = emit_expr(expr->unary.expr, fn);
            struct ir_val dst = make_temp();

            struct ir_instr *instr = calloc(1, sizeof(struct ir_instr));
            *instr = (struct ir_instr){
                .type = IR_UNARY,
                .unary.op = convert_unop(expr->token),
                .unary.src = src,
                .unary.dst = dst,
            };
            append_instr(fn, instr);
            return dst;
        }
        case AST_BINARY: {
            // Special cases for && and || (short-circut)
           if (expr->token.type == TOKEN_AND_AND) {
                // a && b ->
                //  v1 = emit_expr(a); if a == 0 jump false
                //  v2 = emit_expr(b); if b == 0 jump false
                //  dst = 1; jump end
                //  false: dst = 0
                //  end:
                int false_label = make_label();
                int end_label = make_label();
                struct ir_val dst = make_temp();

                struct ir_val v1 = emit_expr(expr->binary.left, fn);
                emit_jump_cond(fn, v1, false_label, IR_JUMP_IF_ZERO);

                struct ir_val v2 = emit_expr(expr->binary.right, fn);
                emit_jump_cond(fn, v2, false_label, IR_JUMP_IF_ZERO);

                emit_copy(fn, make_constant(1), dst);
                emit_jump(fn, end_label);

                emit_label(fn, false_label);

                emit_copy(fn, make_constant(0), dst);

                emit_label(fn, end_label);
                return dst;
            } else if (expr->token.type == TOKEN_OR_OR) {
                // a || b ->
                //  v1 = emit_expr(a); if a != 0 jump true
                //  v2 = emit_expr(b); if b != 0 jump true
                //  dst = 0; jump end
                //  true: dst = 1
                //  end:
                int true_label = make_label();
                int end_label = make_label();
                struct ir_val dst = make_temp();

                struct ir_val v1 = emit_expr(expr->binary.left, fn);
                emit_jump_cond(fn, v1, true_label, IR_JUMP_IF_NOT_ZERO);

                struct ir_val v2 = emit_expr(expr->binary.right, fn);
                emit_jump_cond(fn, v2, true_label, IR_JUMP_IF_NOT_ZERO);

                emit_copy(fn, make_constant(0), dst);
                emit_jump(fn, end_label);

                emit_label(fn, true_label);

                emit_copy(fn, make_constant(1), dst);

                emit_label(fn, end_label);
                return dst;
            }

            // Standard case for binary operations
            // dst = src1 op src2
            struct ir_val v1 = emit_expr(expr->binary.left, fn);
            struct ir_val v2 = emit_expr(expr->binary.right, fn);
            struct ir_val dst = make_temp();

            emit_binary(fn, convert_binop(expr->token), v1, v2, dst);
            return dst;
        }
        case AST_ASSIGNMENT: {
            // Simple assignment (=): copy rvalue into lvalue variable
            if (expr->token.type == TOKEN_EQUAL) {
                struct ir_val lvalue = make_var(&expr->assignment.lvalue->token);
                struct ir_val rvalue = emit_expr(expr->assignment.rvalue, fn);
                emit_copy(fn, rvalue, lvalue);
                return lvalue;
            }

            // Otherwise compound assignment (+= -= &= ...)
            // lvalue = lvalue op rvalue
            struct ir_val lvalue = make_var(&expr->assignment.lvalue->token);
            struct ir_val rvalue = emit_expr(expr->assignment.rvalue, fn);

            emit_binary(fn, convert_binop(expr->token), lvalue, rvalue, lvalue);
            return lvalue;
        }
        case AST_POST:
        case AST_PRE: {
            // Pre (+xx -xx) return x++
            // Post (x++) return save = x, x++
            bool is_incr = expr->token.type == TOKEN_PLUS_PLUS;

            struct ir_val lvalue = make_var(&expr->unary.expr->token);

            // Save old for pre decr/incr
            struct ir_val old_lval;
            if (expr->type == AST_POST) {
                old_lval = make_temp();
                emit_copy(fn, lvalue, old_lval);
            }

            emit_binary(fn, is_incr ? IR_ADD : IR_SUBTRACT, lvalue,
                        make_constant(1), lvalue);
            return expr->type == AST_PRE ? lvalue : old_lval;
        }
        case AST_TERNARY: {
            // cond ? a : b ->
            //  cond_result = emit(cond); if == 0 jump e2
            //  dst = emit(a); jump end
            //  e2: dst = emit(b)
            //  end:
            int e2_label = make_label();
            int end_label = make_label();
            struct ir_val dst = make_temp();

            struct ir_val cond_result = emit_expr(expr->ternary.condition, fn);
            emit_jump_cond(fn, cond_result, e2_label, IR_JUMP_IF_ZERO);

            struct ir_val v1 = emit_expr(expr->ternary.then, fn);
            emit_copy(fn, v1, dst);
            emit_jump(fn, end_label);

            emit_label(fn, e2_label);
            struct ir_val v2 = emit_expr(expr->ternary.else_then, fn);
            emit_copy(fn, v2, dst);

            emit_label(fn, end_label);
            return dst;
        }
        default:
            fprintf(stderr, "tacky_emit: unhandled expr kind\n");
            exit(1);
    }
}

static void emit_block_item(struct ast_node *node, struct ir_function *fn)
{
    switch (node->type) {
        case AST_RETURN: {
            emit_return(fn, emit_expr(node->return_stmt.expr, fn));
            break;
        }
        case AST_DECLARATION: {
            // Emits copy to lvalue if there is initializer
            if (node->declaration.init) {
                struct ir_val lvalue = make_var(&node->declaration.name->token);
                struct ir_val rvalue = emit_expr(node->declaration.init, fn);
                emit_copy(fn, rvalue, lvalue);
            }
            break;
        }
        case AST_IF_STMT: {
            struct ir_val cond = emit_expr(node->if_stmt.condition, fn);
            // No else:
            //  cond = emit(cond); if == 0 jump end
            //  emit(then)
            //  end:
            if (!node->if_stmt.else_then) {
                int end_label = make_label();

                emit_jump_cond(fn, cond, end_label, IR_JUMP_IF_ZERO);
                
                emit_block_item(node->if_stmt.then, fn);

                emit_label(fn, end_label);
            // With else:
            //  cond= = emit(cond); if == 0 jump else
            //  emit(then); jump end
            //  else: emit(else)
            //  end:
            } else {
                int end_label = make_label();
                int else_label = make_label();

                emit_jump_cond(fn, cond, else_label, IR_JUMP_IF_ZERO);

                emit_block_item(node->if_stmt.then, fn);
                emit_jump(fn, end_label);

                emit_label(fn, else_label);
                emit_block_item(node->if_stmt.else_then, fn);

                emit_label(fn, end_label);
            }
            break;
        }
        case AST_BLOCK:
            for (struct ast_node *item = node->block.first; item != NULL; item = item->next)
                emit_block_item(item, fn);
            break;
        case AST_EXPR_STMT:
            emit_expr(node->expr_stmt.expr, fn);
            break;
        case AST_NULL_STMT:
            break;
        case AST_GOTO: {
            struct token *tok = &node->goto_stmt.label;
            int label_id = get_or_create_label_id(tok->start, tok->length);
            emit_jump(fn, label_id);
            break;
        }
        case AST_LABEL_STMT: {
            struct token *tok = &node->label_stmt.name;
            int label_id = get_or_create_label_id(tok->start, tok->length); // After sema we can safely do this, no double labels exist in a function
            emit_label(fn, label_id);
            emit_block_item(node->label_stmt.stmt, fn);
            break;
        }
        case AST_FOR: {
            // for (init; cond; post) body ->
            //  emit(init)
            //  start; if cond == 0 jump break
            //  emit(body)
            //  continue: emit(post)
            //  jump start
            //  break:
            int start_label = make_label();
            int break_label = make_label();
            int continue_label = make_label();

            register_loop_switch(node->for_stmt.label, break_label, continue_label);

            if (node->for_stmt.for_init) {
                if (node->for_stmt.for_init->type == AST_DECLARATION)
                    emit_block_item(node->for_stmt.for_init, fn);
                else
                    emit_expr(node->for_stmt.for_init, fn);
            }

            emit_label(fn, start_label);
            if (node->for_stmt.condition) {
                struct ir_val cond = emit_expr(node->for_stmt.condition, fn);
                emit_jump_cond(fn, cond, break_label, IR_JUMP_IF_ZERO);
            }

            emit_block_item(node->for_stmt.body, fn);
            emit_label(fn, continue_label);

            if (node->for_stmt.post)
                emit_expr(node->for_stmt.post, fn);

            emit_jump(fn, start_label);
            emit_label(fn, break_label);
            break;
        }
        case AST_WHILE: {
            // while (cond) body ->
            //   continue: if cond == 0 jump break
            //   emit(body)
            //   jump continue
            //   break:
            int break_label = make_label();
            int continue_label = make_label();

            register_loop_switch(node->while_stmt.label, break_label, continue_label);

            emit_label(fn, continue_label);
            struct ir_val cond = emit_expr(node->while_stmt.condition, fn);
            emit_jump_cond(fn, cond, break_label, IR_JUMP_IF_ZERO);
            emit_block_item(node->while_stmt.body, fn);
            emit_jump(fn, continue_label);
            emit_label(fn, break_label);
            break;
        }
        case AST_DOWHILE: {
            // do body while (cond) ->
            //   start:
            //   emit(body)
            //   continue: if cond != 0 jump start
            //   break:
            int start_label = make_label();
            int break_label = make_label();
            int continue_label = make_label();

            register_loop_switch(node->do_while.label, break_label, continue_label);

            emit_label(fn, start_label);
            emit_block_item(node->do_while.body, fn);
            emit_label(fn, continue_label);
            struct ir_val cond = emit_expr(node->do_while.condition, fn);
            emit_jump_cond(fn, cond, start_label, IR_JUMP_IF_NOT_ZERO);
            emit_label(fn, break_label);
            break;
        }
        case AST_BREAK: {
            // Lookup enclosing loop/switch label in loop_labels_map
            // Emit break to break_label id
            struct loop_switch_labels *lbls = hashmap_get(&loop_switch_labels_map,
                                                node->break_stmt.target_label,
                                                strlen(node->break_stmt.target_label));
            emit_jump(fn, lbls->break_label);
            break;
        }
        case AST_CONTINUE: {
            // Lookup enclosing loop in loob_labels_map
            struct loop_switch_labels *lbls = hashmap_get(&loop_switch_labels_map,
                                                node->continue_stmt.target_label,
                                                strlen(node->continue_stmt.target_label));
            emit_jump(fn, lbls->continue_label);
            break;
        }
        case AST_SWITCH: {
            int break_label = make_label();
            // Sema handled continue in switches, we can register like this
            register_loop_switch(node->switch_stmt.label, break_label, -1);

            struct ir_val cond = emit_expr(node->switch_stmt.condition, fn);
            struct switch_annotation *ann = node->switch_stmt.annotation;
            
            // First emit comparison chain: cmp cond == case_val, jump to case label
            for (struct case_entry *e = ann->cases; e; e = e->next) {
                if (e->node->type == AST_DEFAULT)
                    continue; // Skip default: it has no condition
                struct ast_node *n = e->node;
                long val = n->case_stmt.value->constant.value;
                int case_label = get_or_create_label_id(n->case_stmt.label,
                        strlen(n->case_stmt.label)) ;
                struct ir_val case_val = make_constant(val);
                struct ir_val cmp_tmp = make_temp();
                emit_binary(fn, IR_EQUAL, cond, case_val, cmp_tmp);
                emit_jump_cond(fn, cmp_tmp, case_label, IR_JUMP_IF_NOT_ZERO);
            }

            // If no case matched, jump to default or fall through break
            if (ann->default_node) {
                int default_label = get_or_create_label_id(ann->default_node->default_stmt.label, strlen(ann->default_node->default_stmt.label));
                emit_jump(fn, default_label);
            } else {
                emit_jump(fn, break_label);
            }

            // Emit case bodies - labels are placed by AST_CASE / AST_DEFAULT handlers
            for (struct case_entry *e = ann->cases; e; e = e->next)
                emit_block_item(e->node, fn);

            emit_label(fn, break_label);
            break;
        }
        case AST_CASE: {
            int label_id = get_or_create_label_id(node->case_stmt.label,
                    strlen(node->case_stmt.label));
            emit_label(fn, label_id);
            for (struct ast_node *item = node->case_stmt.first; item; item = item->next)
                emit_block_item(item, fn);
            break;
        }
        case AST_DEFAULT: {
            int label_id = get_or_create_label_id(node->default_stmt.label,
                    strlen(node->default_stmt.label));
            emit_label(fn, label_id);
            for (struct ast_node *item = node->default_stmt.first; item; item = item->next)
                emit_block_item(item, fn);
            break;
        }
        default:
            fprintf(stderr, "unhandled stmt kind\n");
            exit(1);
    }
}

static struct ir_function *emit_function(struct ast_node *fn_node)
{
    struct ir_function *fn = calloc(1, sizeof(struct ir_function));
    fn->name = fn_node->token.start;
    fn->name_length = fn_node->token.length;

    hashmap_init(&goto_labels);
    hashmap_init(&loop_switch_labels_map);

    for (struct ast_node *item = fn_node->function.body->block.first; item != NULL; item = item->next)
        emit_block_item(item, fn);


    // For main append additional return 0;
    // main must return 0, if no other return
    emit_return(fn, make_constant(0));

    hashmap_free(&goto_labels);
    hashmap_free(&loop_switch_labels_map);
    return fn;
}

struct ir_program *build_tacky(struct ast_node *root)
{
    struct ir_program *program = calloc(1, sizeof(struct ir_program));
    program->function = emit_function(root->program.first);
    return program;
}
