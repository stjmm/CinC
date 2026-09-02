#include "hashmap.h"
#include "memory.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// #define HASHMAP_INITIAL_CAPACITY 16
static constexpr size_t HASHMAP_INITIAL_CAPACITY = 16;

static uint32_t
hash_string(const char *key, size_t len)
{
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619u;
    }

    return hash;
}

static hash_entry *
find_entry(
    hash_entry *entries,
    size_t capacity,
    const char *key,
    size_t key_len)
{
    size_t index = hash_string(key, key_len) & (capacity - 1);

    for (;;) {
        hash_entry *entry = &entries[index];

        if (!entry->key)
            return entry;

        if (entry->key_len == key_len &&
            memcmp(entry->key, key, key_len) == 0) {
            return entry;
        }

        index = (index + 1) & (capacity - 1);
    }
}

static void
resize(hashmap *map, size_t new_capacity)
{
    hash_entry *new_entries =
        xcalloc(new_capacity, sizeof(hash_entry));

    for (size_t i = 0; i < map->capacity; i++) {
        hash_entry *entry = &map->entries[i];

        if (!entry->key)
            continue;

        hash_entry *dest = find_entry(
            new_entries,
            new_capacity,
            entry->key,
            entry->key_len);

        *dest = *entry;
    }

    free(map->entries);

    map->capacity = new_capacity;
    map->entries = new_entries;
}

void
hashmap_init(hashmap *map)
{
    *map = (hashmap){0};

    map->entries =
        xcalloc(HASHMAP_INITIAL_CAPACITY, sizeof(hash_entry));

    map->capacity = HASHMAP_INITIAL_CAPACITY;
}

void
hashmap_free(hashmap *map)
{
    free(map->entries);
    *map = (hashmap){0};
}

void
hashmap_set(
    hashmap *map,
    const char *key,
    size_t key_len, 
    void *value)
{
    if ((map->count + 1) * 4 >= map->capacity * 3) {
        resize(map, map->capacity * 2);
    }

    hash_entry *entry = find_entry(
        map->entries,
        map->capacity,
        key,
        key_len);

    if (!entry->key) {
        entry->key = key;
        entry->key_len = key_len;
        map->count++;
    }

    entry->value = value;
}

void *
hashmap_get(
    hashmap *map,
    const char *key,
    size_t key_len)
{
    if (map->count == 0)
        return nullptr;

    hash_entry *entry = find_entry(
        map->entries,
        map->capacity,
        key,
        key_len);

    return entry->key ? entry->value : nullptr;
}
