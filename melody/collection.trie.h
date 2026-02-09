#pragma once
#include "types.h"
#include "allocator.fwd.h"

#include <string.h>

typedef struct Mel_TrieNode Mel_TrieNode;
struct Mel_TrieNode
{
    Mel_TrieNode** children;
    u8* child_keys;
    u32 child_count;
    u32 child_capacity;
    void* value;
    bool has_value;
};

typedef struct Mel_Trie Mel_Trie;
struct Mel_Trie
{
    Mel_TrieNode* root;
    usize count;
    const Mel_Alloc* allocator;
};

void mel_trie_init(Mel_Trie* t, const Mel_Alloc* alloc);
void mel_trie_free(Mel_Trie* t);
bool mel_trie_insert(Mel_Trie* t, const u8* key, usize key_len, void* value);
void* mel_trie_find(const Mel_Trie* t, const u8* key, usize key_len);
bool mel_trie_remove(Mel_Trie* t, const u8* key, usize key_len);
bool mel_trie_contains(const Mel_Trie* t, const u8* key, usize key_len);
bool mel_trie_starts_with(const Mel_Trie* t, const u8* prefix, usize prefix_len);
usize mel_trie_count(const Mel_Trie* t);
bool mel_trie_empty(const Mel_Trie* t);
void mel_trie_clear(Mel_Trie* t);

#define mel_trie_insert_str(t, str, value) mel_trie_insert((t), (const u8*)(str), strlen(str), (value))
#define mel_trie_find_str(t, str) mel_trie_find((t), (const u8*)(str), strlen(str))
#define mel_trie_remove_str(t, str) mel_trie_remove((t), (const u8*)(str), strlen(str))
#define mel_trie_contains_str(t, str) mel_trie_contains((t), (const u8*)(str), strlen(str))
#define mel_trie_starts_with_str(t, str) mel_trie_starts_with((t), (const u8*)(str), strlen(str))
