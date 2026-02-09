#pragma once

#include "types.h"
#include "allocator.fwd.h"

#define MEL_HASHMAP_EMPTY    0
#define MEL_HASHMAP_OCCUPIED 1
#define MEL_HASHMAP_INIT_CAP 16
#define MEL_HASHMAP_MAX_LOAD 0.75

typedef u64 (*Mel_HashMap_Hash)(const void* key);
typedef bool (*Mel_HashMap_Eq)(const void* a, const void* b);

typedef struct Mel_HashMapEntry Mel_HashMapEntry;
struct Mel_HashMapEntry
{
    void* key;
    void* value;
    u8 state;
};

typedef struct Mel_HashMap Mel_HashMap;
struct Mel_HashMap
{
    Mel_HashMapEntry* entries;
    usize count;
    usize capacity;
    Mel_HashMap_Hash hash;
    Mel_HashMap_Eq eq;
    const Mel_Alloc* allocator;
};

void mel_hashmap_init(Mel_HashMap* hm, Mel_HashMap_Hash hash_fn, Mel_HashMap_Eq eq_fn, const Mel_Alloc* alloc);
void mel_hashmap_free(Mel_HashMap* hm);
bool mel_hashmap_put(Mel_HashMap* hm, void* key, void* value);
void* mel_hashmap_get(Mel_HashMap* hm, void* key);
bool mel_hashmap_contains(Mel_HashMap* hm, void* key);
bool mel_hashmap_remove(Mel_HashMap* hm, void* key);
usize mel_hashmap_count(Mel_HashMap* hm);
bool mel_hashmap_empty(Mel_HashMap* hm);
void mel_hashmap_clear(Mel_HashMap* hm);
void mel_hashmap_reserve(Mel_HashMap* hm, usize capacity);

u64 mel_hashmap_hash_u64(const void* key);
u64 mel_hashmap_hash_str(const void* key);
u64 mel_hashmap_hash_ptr(const void* key);
bool mel_hashmap_eq_u64(const void* a, const void* b);
bool mel_hashmap_eq_str(const void* a, const void* b);

#define mel_hashmap_foreach(hm, key_var, value_var, body) do { \
    for (usize mel__hm_i = 0; mel__hm_i < (hm)->capacity; mel__hm_i++) { \
        if ((hm)->entries[mel__hm_i].state == MEL_HASHMAP_OCCUPIED) { \
            void* key_var = (hm)->entries[mel__hm_i].key; \
            void* value_var = (hm)->entries[mel__hm_i].value; \
            body \
        } \
    } \
} while (0)
