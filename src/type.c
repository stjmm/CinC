#include "type.h"
#include "base/memory.h"

static type_t void_type = {
    .kind = TYPE_VOID
};

static type_t int_type = {
    .kind = TYPE_INT
};

static type_t long_type = {
    .kind = TYPE_LONG
};

type_t *
type_void(void)
{
    return &void_type;
}

type_t *
type_int(void)
{
    return &int_type;
}

type_t *
type_long(void)
{
    return &long_type;
}

type_t *
type_function(
    type_t *return_ty,
    vector params,
    bool variadic)
{
    type_t *ty = xmalloc(sizeof(type_t));
    *ty = (type_t){
        .kind = TYPE_FUNCTION,
        .function = {
            .return_ty = return_ty,
            .params = params,
            .variadic = variadic
        }
    };
    return ty;
}

size_t
type_size(const type_t *ty)
{
    (void)ty;
}

size_t
type_align(const type_t *ty)
{
    (void)ty;
}

bool
type_is_void(const type_t *ty)
{
    return ty->kind == TYPE_VOID;
}

bool
type_is_int(const type_t *ty)
{
    return ty->kind == TYPE_INT;
}

bool
type_is_long(const type_t *ty)
{
    return ty->kind == TYPE_LONG;
}
