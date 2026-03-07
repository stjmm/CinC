#ifndef CINC_ARENA_H
#define CINC_ARENA_H

#include <stddef.h>
#include <stdint.h>

#define ARENA_ALLOC(arena, type) (type*)arena_alloc((arena), sizeof(type))

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
} arena_t;

arena_t *arena_new(size_t capacity);
void *arena_alloc(arena_t *arena, size_t size);
void arena_clear(arena_t *arena);
void arena_destroy(arena_t *arena);

#endif
