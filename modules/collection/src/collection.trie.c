#include <collection.trie/trie.h>
#include <allocator/allocator.h>

static Mel_TrieNode* mel__trie_node_create(const Mel_Alloc* alloc)
{
    Mel_TrieNode* node = mel_alloc_type(alloc, Mel_TrieNode);
    if (!node) return nullptr;
    *node = (Mel_TrieNode){0};
    return node;
}

static void mel__trie_node_free(Mel_TrieNode* node, const Mel_Alloc* alloc)
{
    if (!node) return;
    for (u32 i = 0; i < node->child_count; i++)
    {
        mel__trie_node_free(node->children[i], alloc);
    }
    mel_dealloc(alloc, node->children);
    mel_dealloc(alloc, node->child_keys);
    mel_dealloc(alloc, node);
}

static Mel_TrieNode* mel__trie_node_find_child(Mel_TrieNode* node, u8 key)
{
    for (u32 i = 0; i < node->child_count; i++)
    {
        if (node->child_keys[i] == key) return node->children[i];
    }
    return nullptr;
}

static Mel_TrieNode* mel__trie_node_add_child(Mel_TrieNode* node, u8 key, const Mel_Alloc* alloc)
{
    if (node->child_count == node->child_capacity)
    {
        u32 new_cap = node->child_capacity == 0 ? 4 : node->child_capacity * 2;

        Mel_TrieNode** new_children;
        u8* new_keys;

        if (node->children)
        {
            new_children = (Mel_TrieNode**)mel_realloc(alloc, node->children, sizeof(Mel_TrieNode*) * new_cap);
            new_keys = (u8*)mel_realloc(alloc, node->child_keys, sizeof(u8) * new_cap);
        }
        else
        {
            new_children = (Mel_TrieNode**)mel_alloc(alloc, sizeof(Mel_TrieNode*) * new_cap);
            new_keys = (u8*)mel_alloc(alloc, sizeof(u8) * new_cap);
        }

        if (!new_children || !new_keys) return nullptr;

        node->children = new_children;
        node->child_keys = new_keys;
        node->child_capacity = new_cap;
    }

    Mel_TrieNode* child = mel__trie_node_create(alloc);
    if (!child) return nullptr;

    node->child_keys[node->child_count] = key;
    node->children[node->child_count] = child;
    node->child_count++;

    return child;
}

static void mel__trie_node_remove_child(Mel_TrieNode* node, u32 idx, const Mel_Alloc* alloc)
{
    mel__trie_node_free(node->children[idx], alloc);

    node->child_count--;
    if (idx < node->child_count)
    {
        node->children[idx] = node->children[node->child_count];
        node->child_keys[idx] = node->child_keys[node->child_count];
    }
}

static bool mel__trie_node_is_leaf(Mel_TrieNode* node)
{
    return node->child_count == 0 && !node->has_value;
}

void mel_trie_init(Mel_Trie* t, const Mel_Alloc* alloc)
{
    assert(t);
    assert(alloc);
    t->allocator = alloc;
    t->count = 0;
    t->root = mel__trie_node_create(alloc);
}

void mel_trie_free(Mel_Trie* t)
{
    assert(t);
    mel__trie_node_free(t->root, t->allocator);
    t->root = nullptr;
    t->count = 0;
}

bool mel_trie_insert(Mel_Trie* t, const u8* key, usize key_len, void* value)
{
    assert(t);
    assert(t->root);

    Mel_TrieNode* node = t->root;
    for (usize i = 0; i < key_len; i++)
    {
        Mel_TrieNode* child = mel__trie_node_find_child(node, key[i]);
        if (!child)
        {
            child = mel__trie_node_add_child(node, key[i], t->allocator);
            if (!child) return false;
        }
        node = child;
    }

    if (node->has_value) return false;

    node->value = value;
    node->has_value = true;
    t->count++;
    return true;
}

void* mel_trie_find(const Mel_Trie* t, const u8* key, usize key_len)
{
    assert(t);
    assert(t->root);

    Mel_TrieNode* node = t->root;
    for (usize i = 0; i < key_len; i++)
    {
        node = mel__trie_node_find_child(node, key[i]);
        if (!node) return nullptr;
    }

    if (!node->has_value) return nullptr;
    return node->value;
}

static bool mel__trie_remove_recursive(Mel_TrieNode* node, const u8* key, usize key_len, usize depth, const Mel_Alloc* alloc, bool* removed)
{
    if (depth == key_len)
    {
        if (!node->has_value) return false;
        node->has_value = false;
        node->value = nullptr;
        *removed = true;
        return mel__trie_node_is_leaf(node);
    }

    for (u32 i = 0; i < node->child_count; i++)
    {
        if (node->child_keys[i] == key[depth])
        {
            bool should_delete = mel__trie_remove_recursive(node->children[i], key, key_len, depth + 1, alloc, removed);
            if (should_delete)
            {
                mel__trie_node_remove_child(node, i, alloc);
                return mel__trie_node_is_leaf(node);
            }
            return false;
        }
    }

    return false;
}

bool mel_trie_remove(Mel_Trie* t, const u8* key, usize key_len)
{
    assert(t);
    assert(t->root);

    bool removed = false;
    mel__trie_remove_recursive(t->root, key, key_len, 0, t->allocator, &removed);
    if (removed) t->count--;
    return removed;
}

bool mel_trie_contains(const Mel_Trie* t, const u8* key, usize key_len)
{
    assert(t);
    assert(t->root);

    Mel_TrieNode* node = t->root;
    for (usize i = 0; i < key_len; i++)
    {
        node = mel__trie_node_find_child(node, key[i]);
        if (!node) return false;
    }

    return node->has_value;
}

bool mel_trie_starts_with(const Mel_Trie* t, const u8* prefix, usize prefix_len)
{
    assert(t);
    assert(t->root);

    Mel_TrieNode* node = t->root;
    for (usize i = 0; i < prefix_len; i++)
    {
        node = mel__trie_node_find_child(node, prefix[i]);
        if (!node) return false;
    }

    if (node->has_value) return true;
    return node->child_count > 0;
}

usize mel_trie_count(const Mel_Trie* t)
{
    assert(t);
    return t->count;
}

bool mel_trie_empty(const Mel_Trie* t)
{
    assert(t);
    return t->count == 0;
}

void mel_trie_clear(Mel_Trie* t)
{
    assert(t);
    const Mel_Alloc* alloc = t->allocator;
    mel__trie_node_free(t->root, alloc);
    t->root = mel__trie_node_create(alloc);
    t->count = 0;
}
