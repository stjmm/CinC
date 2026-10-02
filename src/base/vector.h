#ifndef CINC_VECTOR_H
#define CINC_VECTOR_H

#include <stddef.h>

typedef struct {
    void *data;
    size_t count;
    size_t capacity;
    size_t elem_size;
} vector;

#define VECTOR_INIT(vec, type) \
    (*(vec) = (vector){ .elem_size = sizeof(type) })

#define VECTOR_GET(vec, type, index) \
    (&((type *)(vec)->data)[index])

#define VECTOR_GET_LAST(vec, type) \
    (&((type *)(vec)->data)[(vec)->count - 1])

void vector_push(vector *vec, const void *elem);
void vector_pop(vector *vec);
void vector_free(vector *vec);

#endif
