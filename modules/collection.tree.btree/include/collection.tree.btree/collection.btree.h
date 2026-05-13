#pragma once
#include "core.types.h"
#include "allocator.fwd.h"

#define MEL_BTREE_DEFAULT_DEGREE 3

typedef i32 (*Mel_BTree_Cmp)(const void* a, const void* b);

typedef struct Mel_BTreeNode Mel_BTreeNode;
struct Mel_BTreeNode
{
    void** keys;
    void** values;
    Mel_BTreeNode** children;
    u32 key_count;
    bool leaf;
};

typedef struct Mel_BTree Mel_BTree;
struct Mel_BTree
{
    Mel_BTreeNode* root;
    usize count;
    u32 degree;
    Mel_BTree_Cmp cmp;
    const Mel_Alloc* allocator;
};

void mel_btree_init(Mel_BTree* bt, u32 degree, Mel_BTree_Cmp cmp, const Mel_Alloc* alloc);
void mel_btree_free(Mel_BTree* bt);
bool mel_btree_insert(Mel_BTree* bt, void* key, void* value);
void* mel_btree_find(Mel_BTree* bt, void* key);
bool mel_btree_remove(Mel_BTree* bt, void* key);
void* mel_btree_min(Mel_BTree* bt);
void* mel_btree_max(Mel_BTree* bt);
usize mel_btree_count(Mel_BTree* bt);
bool mel_btree_empty(Mel_BTree* bt);
void mel_btree_clear(Mel_BTree* bt);
bool mel_btree_contains(Mel_BTree* bt, void* key);

Mel_BTreeNode* mel__btree_create_node(Mel_BTree* bt, bool leaf);
void mel__btree_free_node(Mel_BTree* bt, Mel_BTreeNode* node);
void mel__btree_split_child(Mel_BTree* bt, Mel_BTreeNode* parent, u32 i);
void mel__btree_insert_nonfull(Mel_BTree* bt, Mel_BTreeNode* node, void* key, void* value);
bool mel__btree_remove_from_node(Mel_BTree* bt, Mel_BTreeNode* node, void* key);
void mel__btree_free_recursive(Mel_BTree* bt, Mel_BTreeNode* node);
