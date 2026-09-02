#include "memory.h"

#include <stdio.h>
#include <stdlib.h>

static void
fatal(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

void *
xmalloc(size_t size)
{
    void *p = malloc(size);

    if (!p && size != 0) {
        fatal("error: malloc failed\n");
    }

    return p;
}

void *
xcalloc(size_t count, size_t size)
{
    void *p = calloc(count, size);

    if (!p && count != 0 && size != 0) {
        fatal("error: calloc failed\n");
    }

    return p;
}

void *
xrealloc(void *ptr, size_t new_size)
{
    void *p = realloc(ptr, new_size);

    if (!p && new_size != 0) {
        fatal("error: realloc failed\n");
    }

    return p;
}
