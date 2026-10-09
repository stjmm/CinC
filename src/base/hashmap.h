#ifndef CINC_HASHMAP_H
#define CINC_HASHMAP_H

#include <stddef.h>

typedef struct {
    const char *key;
    size_t key_len;
    void *value;
} hash_entry;

typedef struct {
    hash_entry *entries;
    size_t count;
    size_t capacity;
} hashmap;

void hashmap_init(hashmap *map);
void hashmap_free(hashmap *map);
void hashmap_set(
    hashmap *map,
    const char *key,
    size_t key_len,
    void *value);
void *hashmap_get(
    hashmap *map,
    const char *key,
    size_t key_len);

#endif
