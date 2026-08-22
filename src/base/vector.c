#include "vector.h"
#include "memory.h"

#include <stdlib.h>
#include <string.h>

static constexpr size_t VECTOR_INITIAL_CAPACITY = 16;

static bool
vector_grow(vector *vec)
{
    size_t new_capacity =
        vec->capacity ? vec->capacity * 2 : VECTOR_INITIAL_CAPACITY;

    void *data = xrealloc(vec->data, new_capacity);
    if (!data)
        return false;

    vec->data = data;
    vec->capacity = new_capacity;

    return true;
}

void
vector_pop(vector *vec)
{
    if (vec->count)
        vec->count--;
}

bool
vector_push(vector *vec, const void *elem)
{
    if (vec->count >= vec->capacity &&
        !vector_grow(vec)) {
        return false;
    }

    void *dest =
        (char *)vec->data + vec->count * vec->elem_size;

    memcpy(dest, elem, vec->elem_size);
    vec->count++;

    return true;
}

void
vector_free(vector *vec)
{
    free(vec->data);
    *vec = (vector){};
}
