#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define ARENA_ALIGN 8
#define ALIGN_UP(n) (((n) + (ARENA_ALIGN - 1)) & ~(ARENA_ALIGN - 1))

arena_t *arena_create(size_t capacity)
{
    arena_t *arena = malloc(sizeof(arena_t));
    if (!arena) return NULL;

    arena->buffer = malloc(capacity);
    if (!arena->buffer) {
        free(arena);
        return NULL;
    }

    arena->capacity = capacity;
    arena->used = 0;

    return arena;
}

void *arena_alloc(arena_t *arena, size_t size)
{
    size_t aligned_used = ALIGN_UP(arena->used);

    if (aligned_used + size > arena->capacity) {
        return NULL;
    }

    void *ptr = arena->buffer + aligned_used;
    arena->used = aligned_used + size;

    memset(ptr, 0, size);

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
    arena->used = 0;
}
