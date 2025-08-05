#ifndef PERFECT_HASHMAP_H
#define PERFECT_HASHMAP_H

#include <stddef.h> // For size_t
#include <stdint.h> // For uint32_t


// --- Data Structures ---

/**
 * @brief Represents a single entry in the hash map.
 */
typedef struct {
    const char* key;
    void* value;
} PerfectHashmapEntry;

/**
 * @brief Represents the perfect hash map.
 */
typedef struct {
    PerfectHashmapEntry* table;
    size_t                   capacity;
    uint32_t                 salt;
} PerfectHashmap;

// --- Public API ---

/**
 * @brief Creates a perfect hash map from a set of keys.
 *
 * This function determines the optimal parameters (minimum key length and salt)
 * to create a collision-free hash map that is exactly the size of the number of keys.
 * The created map is read-only.
 *
 * @param keys An array of unique C-string keys. The pointers must remain valid
 * for the lifetime of the hash map.
 * @param values An array of pointers to the values associated with the keys.
 * @param key_count The number of keys (and values).
 * @return A pointer to the newly created perfect_hashmap_t, or NULL on failure.
 */
void perfect_hashmap_create(PerfectHashmap *map, const char** keys, void** values, size_t key_count);

/**
 * @brief Retrieves a value from the perfect hash map.
 *
 * @param map A pointer to the perfect_hashmap_t.
 * @param key The key to look up.
 * @return A pointer to the value (void**), or NULL if the key is not found.
 */
void* perfect_hashmap_get(PerfectHashmap* map, const char* key);

#endif // PERFECT_HASHMAP_H
