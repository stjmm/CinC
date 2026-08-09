#ifndef CINC_HASH_MAP
#define CINC_HASH_MAP

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
bool hashmap_set(
    hashmap *map,
    const char *key,
    size_t key_len, 
    void *value);
void *hashmap_get(
    hashmap *map,
    const char *key,
    size_t key_len);

#endif
