#ifndef CINC_ARENA_H
#define CINC_ARENA_H

#include <stdint.h>
#include <stddef.h>

#define ARENA_ALLOC(arena, type) (type*)(arena_alloc((arena), sizeof(type)))
#define ARENA_ALLOC_ARRAY(arena, type, count) (type*)(arena_alloc((arena), sizeof(type) * (count)))

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t used;
} arena_t;

arena_t *arena_create(size_t capacity);
void *arena_alloc(arena_t *arena, size_t size);
void arena_destroy(arena_t *arena);
void arena_clear(arena_t *arena);

#endif
