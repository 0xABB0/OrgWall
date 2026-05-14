#pragma once

#include <core/types.h>
#include <collection.map.hashmap/hashmap.h>

typedef struct Mel_Set Mel_Set;
struct Mel_Set
{
    Mel_HashMap map;
};

void mel_set_init(Mel_Set* s, Mel_HashMap_Hash hash_fn, Mel_HashMap_Eq eq_fn, const Mel_Alloc* alloc);
void mel_set_free(Mel_Set* s);
bool mel_set_add(Mel_Set* s, void* key);
bool mel_set_contains(Mel_Set* s, void* key);
bool mel_set_remove(Mel_Set* s, void* key);
usize mel_set_count(Mel_Set* s);
bool mel_set_empty(Mel_Set* s);
void mel_set_clear(Mel_Set* s);
void mel_set_reserve(Mel_Set* s, usize capacity);

void mel_set_union(Mel_Set* dst, Mel_Set* a, Mel_Set* b);
void mel_set_intersection(Mel_Set* dst, Mel_Set* a, Mel_Set* b);
void mel_set_difference(Mel_Set* dst, Mel_Set* a, Mel_Set* b);
void mel_set_symmetric_difference(Mel_Set* dst, Mel_Set* a, Mel_Set* b);
bool mel_set_is_subset(Mel_Set* a, Mel_Set* b);
bool mel_set_is_superset(Mel_Set* a, Mel_Set* b);
bool mel_set_equals(Mel_Set* a, Mel_Set* b);

#define mel_set_foreach(s, key_var, body) do { \
    for (usize mel__set_i = 0; mel__set_i < (s)->map.capacity; mel__set_i++) { \
        if ((s)->map.entries[mel__set_i].state == MEL_HASHMAP_OCCUPIED) { \
            void* key_var = (s)->map.entries[mel__set_i].key; \
            body \
        } \
    } \
} while (0)
