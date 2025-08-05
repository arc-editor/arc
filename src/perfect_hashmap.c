#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "perfect_hashmap.h"
#include "log.h"

// --- Private Functions ---

static uint32_t hash_string(const char* str, uint32_t salt) {
    if (!str) return salt;
    const uint32_t FNV_PRIME = 0x01000193;
    const uint32_t FNV_OFFSET_BASIS = 0x811c9dc5;
    uint32_t hash = FNV_OFFSET_BASIS ^ salt;
    while (*str) {
        hash ^= (uint32_t)(unsigned char)*str++;
        hash *= FNV_PRIME;
    }
    return hash;
}

static bool is_prime(size_t n) {
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    for (size_t i = 5; i * i <= n; i = i + 6) {
        if (n % i == 0 || n % (i + 2) == 0)
            return false;
    }
    return true;
}

static size_t next_prime(size_t n) {
    if (n <= 1) return 2;
    size_t prime = n;
    while (true) {
        if (is_prime(prime)) {
            return prime;
        }
        prime++;
    }
}

static bool check_for_collisions(const char** keys, size_t key_count, size_t capacity, uint32_t salt) {
    const char** occupied_slots = calloc(capacity, sizeof(const char*));
    if (!occupied_slots) {
        log_error("perfect_hashmap.check_for_collisions: failed to allocate memory for collision check");
        return true;
    }

    for (size_t i = 0; i < key_count; ++i) {
        uint32_t hash = hash_string(keys[i], salt);
        size_t index = hash % capacity;
        if (occupied_slots[index]) {
            free(occupied_slots);
            return true; // Collision detected
        }
        occupied_slots[index] = keys[i];
    }

    free(occupied_slots);
    return false; // No collisions
}

// --- Public API ---

void perfect_hashmap_create(PerfectHashmap* map, const char** keys, void** values, size_t key_count) {
    if (!keys || !values || key_count == 0) {
        map->table = NULL;
        map->capacity = 0;
        map->salt = 0;
        return;
    }

    size_t capacity = next_prime(key_count * 2);
    uint32_t found_salt = 0;
    bool found_perfect_hash = false;

    while (!found_perfect_hash) {
        for (uint32_t salt = 123; salt < 567; ++salt) {
            if (!check_for_collisions(keys, key_count, capacity, salt)) {
                found_salt = salt;
                found_perfect_hash = true;
                break;
            }
        }
        if (found_perfect_hash) {
            break;
        }
        capacity = next_prime(capacity + 1);
    }

    map->table = calloc(capacity, sizeof(PerfectHashmapEntry));
    if (!map->table) {
        log_error("perfect_hashmap.perfect_hashmap_create: failed to allocate memory for hash table");
        map->capacity = 0;
        map->salt = 0;
        return;
    }

    map->capacity = capacity;
    map->salt = found_salt;

    for (size_t i = 0; i < key_count; ++i) {
        uint32_t hash = hash_string(keys[i], map->salt);
        size_t index = hash % map->capacity;
        map->table[index].key = keys[i];
        map->table[index].value = values[i];
    }

    log_info("Perfect hash map created successfully! Size: %zu, Capacity: %zu, Salt: %u", key_count, map->capacity, map->salt);
}

void* perfect_hashmap_get(PerfectHashmap* map, const char* key) {
    if (!map || !key || map->capacity == 0 || !map->table) {
        return NULL;
    }

    uint32_t hash = hash_string(key, map->salt);
    size_t index = hash % map->capacity;

    if (map->table[index].key && strcmp(map->table[index].key, key) == 0) {
        return map->table[index].value;
    }

    return NULL;
}
