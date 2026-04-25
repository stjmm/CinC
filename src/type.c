#include <string.h>
#include <stdlib.h>

#include "type.h"

static struct type builtin_void = {
    .kind = TY_VOID,
};

static struct type builtin_int = {
    .kind = TY_INT,
};

struct type *type_void(void)
{
    return &builtin_void;
}

struct type *type_int(void)
{
    return &builtin_int;
}

struct type *type_pointer(struct type *base)
{
    struct type *t = calloc(1, sizeof(struct type));
    t->kind = TY_POINTER;
    t->base = base;
    return t;
}

struct type *type_function(struct type *return_type, struct type **params,
                            int param_count, bool is_variadic)
{
    struct type *t = calloc(1, sizeof(struct type));
    t->kind = TY_FUNCTION;
    t->base = return_type;
    t->param_count = param_count;
    t->is_variadic = is_variadic;

    if (param_count > 0) {
        t->params = calloc(param_count, sizeof(*t->params));
        memcpy(t->params, params, param_count * sizeof(*t->params));
    }
    return t;
}

bool type_equal(struct type *a, struct type *b)
{
    if (a == b)
        return true;

    if (!a || !b || a->kind != b->kind)
        return false;

    switch (a->kind) {
        case TY_VOID:
        case TY_INT:
            return true;
        case TY_POINTER:
            return type_equal(a->base, b->base);
        case TY_FUNCTION:
            if (!type_equal(a->base, b->base))
                return false;

            if (a->param_count != b->param_count)
                return false;

            if (a->is_variadic != b->is_variadic)
                return false;

            for (int i = 0; i < a->param_count; i++) {
                if (!type_equal(a->params[i], b->params[i]))
                    return false;
            }
            
            return true;
    }

    return false;
}
