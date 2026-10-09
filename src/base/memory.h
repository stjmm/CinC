#ifndef CINC_MEMORY_H
#define CINC_MEMORY_H

#include <stddef.h>

void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *ptr, size_t new_size);

#endif
