#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "perfect_hashmap.h"

// --- Private Functions ---

/**
 * @brief A simple string hashing function (djb2).
 *
 * This function computes a hash value for a given string up to a specified length.
 *
 * @param str The string to hash.
 * @param len The number of characters to consider for the hash.
 * @param salt A salt value to add to the hash, allowing for variation.
 * @return The computed 32-bit hash value.
 */
static uint32_t hash_string(const char* str, size_t len, uint32_t salt) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < len && str[i] != '\0'; ++i) {
        hash = ((hash << 5) + hash) + str[i]; // hash * 33 + c
    }
    return hash + salt;
}

/**
 * @brief Checks for hash collisions with a given configuration.
 *
 * This function attempts to place all keys into a hash table of a given size
 * using a specific key length and salt for hashing. It checks if any two keys
 * map to the same index (a collision).
 *
 * @param keys An array of keys to be hashed.
 * @param key_count The number of keys in the array.
 * @param capacity The size of the hash table to test.
 * @param min_key_len The key length to use for hashing.
 * @param salt The salt to use for hashing.
 * @return `true` if no collisions are found, `false` otherwise.
 */
static bool check_for_collisions(const char** keys, size_t key_count, size_t capacity, size_t min_key_len, uint32_t salt) {
    bool* occupied_indices = calloc(capacity, sizeof(bool));
    if (!occupied_indices) {
        perror("Failed to allocate memory for collision check");
        return false; // Cannot proceed if allocation fails
    }

    for (size_t i = 0; i < key_count; ++i) {
        uint32_t hash = hash_string(keys[i], min_key_len, salt);
        size_t index = hash % capacity;
        if (occupied_indices[index]) {
            free(occupied_indices);
            return true; // Collision detected
        }
        occupied_indices[index] = true;
    }

    free(occupied_indices);
    return false; // No collisions
}


// --- Public API ---

/**
 * @brief Creates a perfect hash map from a set of keys.
 *
 * This function determines the optimal parameters (minimum key length and salt)
 * to create a collision-free hash map that is exactly the size of the number of keys.
 *
 * @param keys An array of unique C-string keys.
 * @param values An array of pointers to the values associated with the keys.
 * @param key_count The number of keys (and values).
 * @return A pointer to the newly created perfect_hashmap_t, or NULL on failure.
 */
void perfect_hashmap_create(PerfectHashmap* map, const char** keys, void** values, size_t key_count) {
    if (!keys || !values || key_count == 0) {
        return;
    }

    size_t capacity = key_count; // Smallest possible capacity
    size_t max_key_len = 0;
    for(size_t i = 0; i < key_count; ++i) {
        size_t len = strlen(keys[i]);
        if (len > max_key_len) {
            max_key_len = len;
        }
    }

    size_t   found_min_key_len = 0;
    uint32_t found_salt = 0;
    bool     found_perfect_hash = false;

    // Iterate through possible key lengths
    for (size_t len = 1; len <= max_key_len; ++len) {
        // Iterate through possible salt values
        for (uint32_t salt = 0; salt < 1000; ++salt) { // Limit salt search to prevent infinite loops
            if (!check_for_collisions(keys, key_count, capacity, len, salt)) {
                found_min_key_len = len;
                found_salt = salt;
                found_perfect_hash = true;
                goto found; // Exit both loops
            }
        }
    }

found:
    if (!found_perfect_hash) {
        fprintf(stderr, "Could not find a perfect hash function within search limits.\n");
        return;
    }

    map->table = calloc(capacity, sizeof(PerfectHashmapEntry));
    if (!map->table) {
        perror("Failed to allocate memory for hash table");
        free(map);
        return;
    }

    map->capacity = capacity;
    map->min_key_len = found_min_key_len;
    map->salt = found_salt;

    // Populate the table with the determined perfect hash
    for (size_t i = 0; i < key_count; ++i) {
        uint32_t hash = hash_string(keys[i], map->min_key_len, map->salt);
        size_t index = hash % map->capacity;
        map->table[index].key = keys[i];
        map->table[index].value = &values[i]; // Store the address of the void*
    }

    // printf("Perfect hash map created successfully!\n");
    // printf("  - Capacity: %zu\n", map->capacity);
    // printf("  - Min Key Length: %zu\n", map->min_key_len);
    // printf("  - Salt: %u\n", map->salt);
}

/**
 * @brief Retrieves a value from the perfect hash map.
 *
 * @param map A pointer to the perfect_hashmap_t.
 * @param key The key to look up.
 * @return A pointer to the value (void**), or NULL if the key is not found.
 */
void* perfect_hashmap_get(PerfectHashmap* map, const char* key) {
    if (!map || !key) {
        return NULL;
    }

    uint32_t hash = hash_string(key, map->min_key_len, map->salt);
    size_t index = hash % map->capacity;

    // Since it's a perfect hash, we just need to check if the key at the
    // computed index is the one we are looking for.
    if (map->table[index].key && strcmp(map->table[index].key, key) == 0) {
        return map->table[index].value;
    }

    return NULL; // Key not found
}
