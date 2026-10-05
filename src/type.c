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
    type_t *ty = xcalloc(1, sizeof(type_t));
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
    switch (ty->kind) {
        case TYPE_INT:
            return 4;
        case TYPE_LONG:
            return 8;
        default:
            return 0;
    }
}

size_t
type_align(const type_t *ty)
{
    return type_size(ty);
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

bool
type_is_function(const type_t *ty)
{
    return ty->kind == TYPE_FUNCTION;
}

bool
type_is_object(const type_t *ty)
{
    return ty && !type_is_function(ty) &&
        !type_is_void(ty);
}

bool
type_is_integer(const type_t *ty)
{
    return type_is_int(ty) ||
        type_is_long(ty);
}

bool
type_is_arithmetic(const type_t *ty)
{
    return type_is_integer(ty);
}

bool
type_is_scalar(const type_t *ty)
{
    return type_is_integer(ty);
}

bool
type_compatible(const type_t *a, const type_t *b)
{
    if (a == b)
        return true;

    if (!a || !b)
        return false;

    if (a->kind != b->kind)
        return false;

    switch (a->kind) {
        case TYPE_VOID:
        case TYPE_INT:
        case TYPE_LONG:
            return true;
        case TYPE_FUNCTION:
            if (!type_compatible(a->function.return_ty,
                        b->function.return_ty)) {
                return false;
            }

            if (a->function.params.count !=
                    b->function.params.count) {
                return false;
            }

            for (size_t i = 0; i < a->function.params.count; i++) {
                type_t *param_a =
                    *VECTOR_GET(&a->function.params, type_t *, i);
                type_t *param_b =
                    *VECTOR_GET(&b->function.params, type_t *, i);

                if (!type_compatible(param_a, param_b))
                    return false;
            }

            return true;
    }
    
    return false;
}

type_t *
type_usual_arithmetic_conversion(type_t *a, type_t *b)
{
    if (a == b)
        return a;
    else
        return type_long();
}
