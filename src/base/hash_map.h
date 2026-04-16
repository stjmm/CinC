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

void hm_init(hash_map *hm);
void hm_free(hash_map *hm);
bool hm_set(hash_map *hm, const char *key, int key_len, void *value);
void *hm_get(hash_map *hm, const char *key, int key_len);

#endif
