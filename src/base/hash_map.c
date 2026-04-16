#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hash_map.h"

#define HM_INITIAL_CAP 16
#define HM_LOAD_FACTOR 0.75

static uint32_t fnv1a_hash(const char *key, int len)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < len; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static hm_entry *find_entry(hm_entry *entries, size_t capacity,
                            const char *key, int key_len)
{
    uint32_t idx = fnv1a_hash(key, key_len) % capacity;
    for(;;) {
        hm_entry *entry = &entries[idx];
        if (!entry->key)
            return entry; // Empty entry
        if (entry->key_len == key_len && (memcmp(entry->key, key, key_len)) == 0)
            return entry;
        idx = (idx + 1) % capacity;
    }
}

static void grow_capacity(hash_map *hm, size_t capacity)
{
    hm_entry *entries = calloc(capacity, sizeof(hm_entry));

    for (size_t i = 0; i < hm->capacity; i++) {
        hm_entry *entry = &hm->entries[i];
        if (entry->key == NULL)
            continue;

        hm_entry *dest = find_entry(entries, capacity, entry->key, entry->key_len);
        *dest = *entry;
    }

    free(hm->entries);
    hm->entries = entries;
    hm->capacity = capacity;
}

void hm_init(hash_map *hm)
{
    hm->count = 0;
    hm->capacity = HM_INITIAL_CAP;
    hm->entries = calloc(hm->capacity, sizeof(hm_entry));
}

void hm_free(hash_map *hm)
{
    free(hm->entries);
    hm->entries = NULL;
    hm->capacity = 0;
    hm->count = 0;
}

bool hm_set(hash_map *hm, const char *key, int key_len, void *value)
{
    if (hm->count >= (size_t)(hm->capacity * HM_LOAD_FACTOR))
        grow_capacity(hm, hm->capacity * 2);

    hm_entry *entry = find_entry(hm->entries, hm->capacity, key, key_len);
    bool is_new = entry->key == NULL;
    if (is_new) hm->count++;

    entry->key = key;
    entry->key_len = key_len;
    entry->value = value;

    return is_new;
}

void *hm_get(hash_map *hm, const char *key, int key_len)
{
    hm_entry *entry = find_entry(hm->entries, hm->capacity, key, key_len);
    return entry->key ? entry->value : NULL;
}
