#pragma once
#include "core.types.h"
#include "allocator.fwd.h"

#define MEL_SKIPLIST_MAX_LEVEL 32

typedef i32 (*Mel_SkipList_Cmp)(const void* a, const void* b);

typedef struct Mel_SkipNode Mel_SkipNode;
struct Mel_SkipNode
{
    void* key;
    void* value;
    u32 level;
    Mel_SkipNode* forward[];
};

typedef struct Mel_SkipList Mel_SkipList;
struct Mel_SkipList
{
    Mel_SkipNode* header;
    usize count;
    u32 level;
    u64 rng_state;
    Mel_SkipList_Cmp cmp;
    const Mel_Alloc* allocator;
};

void mel_skiplist_init(Mel_SkipList* sl, Mel_SkipList_Cmp cmp, const Mel_Alloc* alloc);
void mel_skiplist_free(Mel_SkipList* sl);
bool mel_skiplist_insert(Mel_SkipList* sl, void* key, void* value);
Mel_SkipNode* mel_skiplist_find(Mel_SkipList* sl, void* key);
bool mel_skiplist_remove(Mel_SkipList* sl, void* key);
Mel_SkipNode* mel_skiplist_min(Mel_SkipList* sl);
Mel_SkipNode* mel_skiplist_max(Mel_SkipList* sl);
usize mel_skiplist_count(Mel_SkipList* sl);
bool mel_skiplist_empty(Mel_SkipList* sl);
void mel_skiplist_clear(Mel_SkipList* sl);
bool mel_skiplist_contains(Mel_SkipList* sl, void* key);
void mel_skiplist_seed(Mel_SkipList* sl, u64 seed);
