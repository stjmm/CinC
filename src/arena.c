#include <stdlib.h>

#include "arena.h"

#define ARENA_ALIGN 8
#define ALIGN_UP(n) (((n) + (ARENA_ALIGN - 1)) & ~(ARENA_ALIGN - 1))

arena_t *arena_new(size_t capacity)
{
    arena_t *arena = malloc(sizeof(arena_t));
    if (!arena) return NULL;

    arena->buffer = malloc(capacity);
    if (!arena->buffer) return NULL;

    arena->capacity = capacity;
    arena->offset = 0;

    return arena;
}

void *arena_alloc(arena_t *arena, size_t size)
{
    size_t aligned_offset = ALIGN_UP(arena->offset);

    if (aligned_offset + size > arena->capacity) return NULL;

    void *ptr = arena->buffer + aligned_offset;
    arena->offset = aligned_offset + size;

    return ptr;
}

void arena_destroy(arena_t *arena)
{
    if (arena) {
        free(arena->buffer);
        free(arena);
    }
}

void arena_clear(arena_t *arena)
{
    arena->offset = 0;
}
