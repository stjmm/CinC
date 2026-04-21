#ifndef CINC_HASH_MAP
#define CINC_HASH_MAP

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *key;
    int key_len;
    void *value;
} hm_entry;

typedef struct {
    hm_entry *entries;
    size_t count;
    size_t capacity;
} hash_map;

void hashmap_init(hash_map *hm);
void hashmap_free(hash_map *hm);
bool hashmap_set(hash_map *hm, const char *key, int key_len, void *value);
void *hashmap_get(hash_map *hm, const char *key, int key_len);

#endif
