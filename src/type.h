#ifndef CINC_TYPE_H
#define CINC_TYPE_H

#include "base/vector.h"

#include <stddef.h>

typedef struct type_t type_t;

typedef enum {
    TYPE_VOID,
    TYPE_INT,
    TYPE_LONG,
    TYPE_FUNCTION
} type_kind;

struct type_t {
    type_kind kind;

    union {
        struct {
            type_t *return_ty;
            vector params;
            bool variadic;
        } function;
    };
};

type_t *type_void(void);
type_t *type_int(void);
type_t *type_long(void);
type_t *type_function(
    type_t *return_ty,
    vector params,
    bool variadic);

size_t type_size(const type_t *ty);
size_t type_align(const type_t *ty);

bool type_is_void(const type_t *ty);
bool type_is_int(const type_t *ty);
bool type_is_long(const type_t *ty);

bool type_is_function(const type_t *ty);
bool type_is_object(const type_t *ty);
bool type_is_integer(const type_t *ty);
bool type_is_arithmetic(const type_t *ty);
bool type_is_scalar(const type_t *ty);

bool type_compatible(const type_t *a, const type_t *b);
type_t *type_composite(const type_t *a, const type_t *b);
type_t *type_usual_arithmetic_conversion(
    const type_t *a,
    const type_t *b);

#endif
