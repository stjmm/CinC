#include "vector.h"
#include "memory.h"

#include <stdlib.h>
#include <string.h>

static constexpr size_t VECTOR_INITIAL_CAPACITY = 16;

static void
vector_grow(vector *vec)
{
    size_t new_capacity =
        vec->capacity ? vec->capacity * 2 : VECTOR_INITIAL_CAPACITY;

    vec->data = xrealloc(vec->data, new_capacity * vec->elem_size);
    vec->capacity = new_capacity;
}

void
vector_pop(vector *vec)
{
    if (vec->count)
        vec->count--;
}

void
vector_push(vector *vec, const void *elem)
{
    if (vec->count >= vec->capacity)
        vector_grow(vec);

    void *dest =
        (char *)vec->data + vec->count * vec->elem_size;

    memcpy(dest, elem, vec->elem_size);
    vec->count++;
}

void
vector_free(vector *vec)
{
    free(vec->data);
    *vec = (vector){};
}
