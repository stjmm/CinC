#include <stdlib.h>

#include "ast.h"
#include "type.h"

static struct type builtin_void = {
    .kind = TY_VOID,
    .size = 0,
    .align = 1
};

static struct type builtin_int = {
    .kind = TY_INT,
    .size = 4,
    .align = 4
};

struct type *type_void(void)
{
    return &builtin_void;
}

struct type *type_int(void)
{
    return &builtin_int;
}

struct type *type_function(struct type *return_type, struct decl *params, int param_count, bool has_prototype)
{
    struct type *t = calloc(1, sizeof(struct type));
    t->kind = TY_FUNCTION;
    t->func.return_type = return_type;
    t->func.params = params;
    t->func.param_count = param_count;
    t->func.has_prototype = has_prototype;
    return t;
}

bool type_is_void(struct type *ty)
{
    return ty && ty->kind == TY_VOID;
}

bool type_is_int(struct type *ty)
{
    return ty && ty->kind == TY_INT;
}

bool type_is_function(struct type *ty)
{
    return ty && ty->kind == TY_FUNCTION;
}

bool type_is_object(struct type *ty)
{
    return ty && ty->kind != TY_FUNCTION && ty->kind != TY_VOID;
}

bool types_compatible(struct type *a, struct type *b)
{
    if (a == b)
        return true;

    if (!a || !b)
        return false;

    if (a->kind != b->kind)
        return false;

    switch (a->kind) {
        case TY_VOID:
        case TY_INT:
            return true;

        case TY_FUNCTION:
            if (!types_compatible(a->func.return_type, b->func.return_type))
                return false;

            /*
             * int f() is a non prototype declaration
             * compatible with declarations with prototypes
             */
            if (!a->func.has_prototype || !b->func.has_prototype)
                return true;

            if (a->func.param_count != b->func.param_count)
                return false;

            struct decl *pa = a->func.params;
            struct decl *pb = b->func.params;
            for (; pa && pb; pa = pa->next, pb = pb->next)
                if (!types_compatible(pa->ty, pb->ty))
                    return false;

            return pa == NULL && pb == NULL;
    }

    return false;
}
