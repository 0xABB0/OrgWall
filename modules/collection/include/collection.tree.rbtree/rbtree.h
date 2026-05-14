#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#define MEL_RB_RED   0
#define MEL_RB_BLACK 1

typedef i32 (*Mel_RBTree_Cmp)(const void* a, const void* b);

typedef struct Mel_RBNode Mel_RBNode;
struct Mel_RBNode
{
    Mel_RBNode* left;
    Mel_RBNode* right;
    Mel_RBNode* parent;
    void* key;
    void* value;
    u8 color;
};

typedef struct Mel_RBTree Mel_RBTree;
struct Mel_RBTree
{
    Mel_RBNode* root;
    Mel_RBNode nil_node;
    usize count;
    Mel_RBTree_Cmp cmp;
    const Mel_Alloc* allocator;
};

void mel_rbtree_init(Mel_RBTree* tree, Mel_RBTree_Cmp cmp, const Mel_Alloc* alloc);
void mel_rbtree_free(Mel_RBTree* tree);
bool mel_rbtree_insert(Mel_RBTree* tree, void* key, void* value);
Mel_RBNode* mel_rbtree_find(Mel_RBTree* tree, void* key);
bool mel_rbtree_remove(Mel_RBTree* tree, void* key);
Mel_RBNode* mel_rbtree_min(Mel_RBTree* tree);
Mel_RBNode* mel_rbtree_max(Mel_RBTree* tree);
Mel_RBNode* mel_rbtree_next(Mel_RBTree* tree, Mel_RBNode* node);
Mel_RBNode* mel_rbtree_prev(Mel_RBTree* tree, Mel_RBNode* node);
usize mel_rbtree_count(Mel_RBTree* tree);
bool mel_rbtree_empty(Mel_RBTree* tree);
void mel_rbtree_clear(Mel_RBTree* tree);
bool mel_rbtree_contains(Mel_RBTree* tree, void* key);

static inline bool mel_rbtree_is_nil(Mel_RBTree* tree, Mel_RBNode* node)
{
    return node == &tree->nil_node;
}
