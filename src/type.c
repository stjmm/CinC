#include <stdlib.h>

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

struct type *type_function(struct type *return_type, struct decl *params, bool has_prototype)
{
    struct type *t = calloc(1, sizeof(struct type));
    t->kind = TY_FUNCTION;
    t->func.return_type = return_type;
    t->func.params = params;
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
