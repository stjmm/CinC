#include <stdlib.h>

#include "asm.h"

static instr_t *instr_new(instr_kind_e kind)
{
    instr_t *i = malloc(sizeof(instr_t));
    i->kind = kind;
    return i;
}

static operand_t op_imm(long value) {
    return (operand_t){ .kind = OPERAND_IMM, .imm = { .value = value } };
}

static operand_t op_reg(reg_e reg) {
    return (operand_t){ .kind = OPERAND_REG, .reg = { .reg = reg } };
}

static void append(instr_t **head, instr_t **tail, instr_t *i) {
    if (!*head) *head = i; else (*tail)->next = i;
    *tail = i;
}

static void emit_mov(instr_t **head, instr_t **tail, operand_t src, operand_t dst) {
    instr_t *i = instr_new(INSTR_MOV);
    i->mov.src = src;
    i->mov.dst = dst;
    append(head, tail, i);
}

static void emit_ret(instr_t **head, instr_t **tail) {
    append(head, tail, instr_new(INSTR_RET));
}

static void print_operand(FILE *f, operand_t op) {
    switch (op.kind) {
        case OPERAND_IMM: fprintf(f, "$%ld", op.imm.value); break;
        case OPERAND_REG:
            switch (op.reg.reg) {
                case REG_EAX: fprintf(f, "%%eax"); break;
            }
            break;
    }
}

static void print_instr(FILE *f, instr_t *i) {
    switch (i->kind) {
        case INSTR_MOV:
            fprintf(f, "    movl ");
            print_operand(f, i->mov.src);
            fprintf(f, ", ");
            print_operand(f, i->mov.dst);
            fprintf(f, "\n");
            break;
        case INSTR_RET:
            fprintf(f, "    ret\n");
            break;
    }
}

static void print_function(FILE *f, asm_function_t *fn) {
    fprintf(f, "    .globl %.*s\n", fn->name_length, fn->name);
    fprintf(f, "%.*s:\n", fn->name_length, fn->name);
    for (instr_t *i = fn->first; i; i = i->next)
        print_instr(f, i);
}

void asm_print(FILE *f, asm_program_t *prog) {
    print_function(f, prog->first);
}

static asm_function_t *gen_function(ast_node_t *fn)
{
    asm_function_t *afn = calloc(1, sizeof(asm_function_t));
    afn->name = fn->function.name.start;
    afn->name_length = fn->function.name.length;

    instr_t *head = NULL, *tail = NULL;

    ast_node_t *stmt = fn->function.body->block.first;
    while (stmt) {
        if (stmt->kind == AST_RETURN) {
            long val = stmt->return_stmt.expr->constant.value;
            emit_mov(&head, &tail, op_imm(val), op_reg(REG_EAX));
            emit_ret(&head, &tail);
        }
        stmt = stmt->next;
    }

    afn->first = head;

    return afn;
}


void codegen(ast_node_t *root, FILE *f)
{
    asm_program_t *prog = malloc(sizeof(asm_program_t));
    prog->first = gen_function(root->program.first);
    asm_print(f, prog);
}
