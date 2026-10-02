#include "sema.h"
#include "diagnostics.h"
#include "base/hashmap.h"

typedef struct scope {
    struct scope *parent;
    hashmap ordinary;
} scope_t;

typedef struct {
    sema_result_t *result;

    scope_t *current_scope;
    scope_t global_scope;

    ast_decl_t *current_function;

    hashmap external_symbols;
    hashmap internal_symbols;
} sema_state_t;

static sema_state_t sema;

bool
sema_analyze(
    sema_result_t *result,
    ast_program_t *program)
{
    *result = (sema_result_t){
        .program = program
    };

    sema = (sema_state_t){
        .result = result
    };

    return !diagnostics_had_error();
}
