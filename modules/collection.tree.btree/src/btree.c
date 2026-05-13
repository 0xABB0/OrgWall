#include "btree.h"
#include "allocator.h"

void mel_btree_init(Mel_BTree* bt, u32 degree, Mel_BTree_Cmp cmp, const Mel_Alloc* alloc)
{
    assert(bt != NULL);
    assert(degree >= 2);
    assert(cmp != NULL);
    assert(alloc != NULL);
    bt->root = NULL;
    bt->count = 0;
    bt->degree = degree;
    bt->cmp = cmp;
    bt->allocator = alloc;
}

Mel_BTreeNode* mel__btree_create_node(Mel_BTree* bt, bool leaf)
{
    u32 t = bt->degree;
    u32 max_keys = 2 * t - 1;
    u32 max_children = 2 * t;

    Mel_BTreeNode* node = mel_alloc_type(bt->allocator, Mel_BTreeNode);
    node->keys = mel_alloc_array(bt->allocator, void*, max_keys);
    node->values = mel_alloc_array(bt->allocator, void*, max_keys);
    node->children = mel_alloc_array(bt->allocator, Mel_BTreeNode*, max_children);
    node->key_count = 0;
    node->leaf = leaf;
    return node;
}

void mel__btree_free_node(Mel_BTree* bt, Mel_BTreeNode* node)
{
    if (!node) return;
    mel_dealloc(bt->allocator, node->keys);
    mel_dealloc(bt->allocator, node->values);
    mel_dealloc(bt->allocator, node->children);
    mel_dealloc(bt->allocator, node);
}

void mel__btree_free_recursive(Mel_BTree* bt, Mel_BTreeNode* node)
{
    if (!node) return;
    if (!node->leaf)
    {
        for (u32 i = 0; i <= node->key_count; i++)
        {
            mel__btree_free_recursive(bt, node->children[i]);
        }
    }
    mel__btree_free_node(bt, node);
}

void mel_btree_free(Mel_BTree* bt)
{
    mel__btree_free_recursive(bt, bt->root);
    bt->root = NULL;
    bt->count = 0;
}

void mel_btree_clear(Mel_BTree* bt)
{
    mel_btree_free(bt);
}

void mel__btree_split_child(Mel_BTree* bt, Mel_BTreeNode* parent, u32 i)
{
    u32 t = bt->degree;
    Mel_BTreeNode* full = parent->children[i];
    Mel_BTreeNode* right = mel__btree_create_node(bt, full->leaf);

    right->key_count = t - 1;

    for (u32 j = 0; j < t - 1; j++)
    {
        right->keys[j] = full->keys[j + t];
        right->values[j] = full->values[j + t];
    }

    if (!full->leaf)
    {
        for (u32 j = 0; j < t; j++)
        {
            right->children[j] = full->children[j + t];
        }
    }

    full->key_count = t - 1;

    for (u32 j = parent->key_count; j > i; j--)
    {
        parent->children[j + 1] = parent->children[j];
    }
    parent->children[i + 1] = right;

    for (u32 j = parent->key_count; j > i; j--)
    {
        parent->keys[j] = parent->keys[j - 1];
        parent->values[j] = parent->values[j - 1];
    }
    parent->keys[i] = full->keys[t - 1];
    parent->values[i] = full->values[t - 1];
    parent->key_count++;
}

void mel__btree_insert_nonfull(Mel_BTree* bt, Mel_BTreeNode* node, void* key, void* value)
{
    i32 i = (i32)node->key_count - 1;

    if (node->leaf)
    {
        while (i >= 0 && bt->cmp(key, node->keys[i]) < 0)
        {
            node->keys[i + 1] = node->keys[i];
            node->values[i + 1] = node->values[i];
            i--;
        }
        node->keys[i + 1] = key;
        node->values[i + 1] = value;
        node->key_count++;
    }
    else
    {
        while (i >= 0 && bt->cmp(key, node->keys[i]) < 0)
        {
            i--;
        }
        i++;

        if (node->children[i]->key_count == 2 * bt->degree - 1)
        {
            mel__btree_split_child(bt, node, (u32)i);
            if (bt->cmp(key, node->keys[i]) > 0)
            {
                i++;
            }
        }
        mel__btree_insert_nonfull(bt, node->children[i], key, value);
    }
}

static bool mel__btree_find_in_node(Mel_BTree* bt, Mel_BTreeNode* node, void* key, void** out_value)
{
    if (!node) return false;

    u32 i = 0;
    while (i < node->key_count && bt->cmp(key, node->keys[i]) > 0)
    {
        i++;
    }

    if (i < node->key_count && bt->cmp(key, node->keys[i]) == 0)
    {
        if (out_value) *out_value = node->values[i];
        return true;
    }

    if (node->leaf) return false;

    return mel__btree_find_in_node(bt, node->children[i], key, out_value);
}

bool mel_btree_insert(Mel_BTree* bt, void* key, void* value)
{
    if (mel__btree_find_in_node(bt, bt->root, key, NULL))
    {
        return false;
    }

    if (!bt->root)
    {
        bt->root = mel__btree_create_node(bt, true);
        bt->root->keys[0] = key;
        bt->root->values[0] = value;
        bt->root->key_count = 1;
        bt->count = 1;
        return true;
    }

    if (bt->root->key_count == 2 * bt->degree - 1)
    {
        Mel_BTreeNode* new_root = mel__btree_create_node(bt, false);
        new_root->children[0] = bt->root;
        bt->root = new_root;
        mel__btree_split_child(bt, new_root, 0);
        mel__btree_insert_nonfull(bt, new_root, key, value);
    }
    else
    {
        mel__btree_insert_nonfull(bt, bt->root, key, value);
    }

    bt->count++;
    return true;
}

void* mel_btree_find(Mel_BTree* bt, void* key)
{
    void* value = NULL;
    if (mel__btree_find_in_node(bt, bt->root, key, &value))
    {
        return value;
    }
    return NULL;
}

bool mel_btree_contains(Mel_BTree* bt, void* key)
{
    return mel__btree_find_in_node(bt, bt->root, key, NULL);
}

void* mel_btree_min(Mel_BTree* bt)
{
    if (!bt->root) return NULL;
    Mel_BTreeNode* node = bt->root;
    while (!node->leaf)
    {
        node = node->children[0];
    }
    if (node->key_count == 0) return NULL;
    return node->keys[0];
}

void* mel_btree_max(Mel_BTree* bt)
{
    if (!bt->root) return NULL;
    Mel_BTreeNode* node = bt->root;
    while (!node->leaf)
    {
        node = node->children[node->key_count];
    }
    if (node->key_count == 0) return NULL;
    return node->keys[node->key_count - 1];
}

usize mel_btree_count(Mel_BTree* bt)
{
    return bt->count;
}

bool mel_btree_empty(Mel_BTree* bt)
{
    return bt->count == 0;
}

static void mel__btree_borrow_from_prev(Mel_BTreeNode* parent, u32 idx)
{
    Mel_BTreeNode* child = parent->children[idx];
    Mel_BTreeNode* sibling = parent->children[idx - 1];

    for (u32 i = child->key_count; i > 0; i--)
    {
        child->keys[i] = child->keys[i - 1];
        child->values[i] = child->values[i - 1];
    }

    if (!child->leaf)
    {
        for (u32 i = child->key_count + 1; i > 0; i--)
        {
            child->children[i] = child->children[i - 1];
        }
        child->children[0] = sibling->children[sibling->key_count];
    }

    child->keys[0] = parent->keys[idx - 1];
    child->values[0] = parent->values[idx - 1];

    parent->keys[idx - 1] = sibling->keys[sibling->key_count - 1];
    parent->values[idx - 1] = sibling->values[sibling->key_count - 1];

    child->key_count++;
    sibling->key_count--;
}

static void mel__btree_borrow_from_next(Mel_BTreeNode* parent, u32 idx)
{
    Mel_BTreeNode* child = parent->children[idx];
    Mel_BTreeNode* sibling = parent->children[idx + 1];

    child->keys[child->key_count] = parent->keys[idx];
    child->values[child->key_count] = parent->values[idx];

    if (!child->leaf)
    {
        child->children[child->key_count + 1] = sibling->children[0];
    }

    parent->keys[idx] = sibling->keys[0];
    parent->values[idx] = sibling->values[0];

    for (u32 i = 1; i < sibling->key_count; i++)
    {
        sibling->keys[i - 1] = sibling->keys[i];
        sibling->values[i - 1] = sibling->values[i];
    }

    if (!sibling->leaf)
    {
        for (u32 i = 1; i <= sibling->key_count; i++)
        {
            sibling->children[i - 1] = sibling->children[i];
        }
    }

    child->key_count++;
    sibling->key_count--;
}

static void mel__btree_merge(Mel_BTree* bt, Mel_BTreeNode* parent, u32 idx)
{
    u32 t = bt->degree;
    Mel_BTreeNode* left = parent->children[idx];
    Mel_BTreeNode* right = parent->children[idx + 1];

    left->keys[t - 1] = parent->keys[idx];
    left->values[t - 1] = parent->values[idx];

    for (u32 i = 0; i < right->key_count; i++)
    {
        left->keys[t + i] = right->keys[i];
        left->values[t + i] = right->values[i];
    }

    if (!left->leaf)
    {
        for (u32 i = 0; i <= right->key_count; i++)
        {
            left->children[t + i] = right->children[i];
        }
    }

    left->key_count = 2 * t - 1;

    for (u32 i = idx + 1; i < parent->key_count; i++)
    {
        parent->keys[i - 1] = parent->keys[i];
        parent->values[i - 1] = parent->values[i];
    }

    for (u32 i = idx + 2; i <= parent->key_count; i++)
    {
        parent->children[i - 1] = parent->children[i];
    }

    parent->key_count--;

    mel__btree_free_node(bt, right);
}

static void mel__btree_fill(Mel_BTree* bt, Mel_BTreeNode* node, u32 idx)
{
    u32 t = bt->degree;

    if (idx > 0 && node->children[idx - 1]->key_count >= t)
    {
        mel__btree_borrow_from_prev(node, idx);
    }
    else if (idx < node->key_count && node->children[idx + 1]->key_count >= t)
    {
        mel__btree_borrow_from_next(node, idx);
    }
    else
    {
        if (idx < node->key_count)
        {
            mel__btree_merge(bt, node, idx);
        }
        else
        {
            mel__btree_merge(bt, node, idx - 1);
        }
    }
}

static void* mel__btree_get_predecessor_key(Mel_BTreeNode* node)
{
    Mel_BTreeNode* cur = node;
    while (!cur->leaf)
    {
        cur = cur->children[cur->key_count];
    }
    return cur->keys[cur->key_count - 1];
}

static void* mel__btree_get_predecessor_value(Mel_BTreeNode* node)
{
    Mel_BTreeNode* cur = node;
    while (!cur->leaf)
    {
        cur = cur->children[cur->key_count];
    }
    return cur->values[cur->key_count - 1];
}

static void* mel__btree_get_successor_key(Mel_BTreeNode* node)
{
    Mel_BTreeNode* cur = node;
    while (!cur->leaf)
    {
        cur = cur->children[0];
    }
    return cur->keys[0];
}

static void* mel__btree_get_successor_value(Mel_BTreeNode* node)
{
    Mel_BTreeNode* cur = node;
    while (!cur->leaf)
    {
        cur = cur->children[0];
    }
    return cur->values[0];
}

static void mel__btree_remove_from_leaf(Mel_BTreeNode* node, u32 idx)
{
    for (u32 i = idx + 1; i < node->key_count; i++)
    {
        node->keys[i - 1] = node->keys[i];
        node->values[i - 1] = node->values[i];
    }
    node->key_count--;
}

static bool mel__btree_remove_from_internal(Mel_BTree* bt, Mel_BTreeNode* node, u32 idx)
{
    u32 t = bt->degree;
    void* key = node->keys[idx];

    if (node->children[idx]->key_count >= t)
    {
        void* pred_key = mel__btree_get_predecessor_key(node->children[idx]);
        void* pred_val = mel__btree_get_predecessor_value(node->children[idx]);
        node->keys[idx] = pred_key;
        node->values[idx] = pred_val;
        return mel__btree_remove_from_node(bt, node->children[idx], pred_key);
    }
    else if (node->children[idx + 1]->key_count >= t)
    {
        void* succ_key = mel__btree_get_successor_key(node->children[idx + 1]);
        void* succ_val = mel__btree_get_successor_value(node->children[idx + 1]);
        node->keys[idx] = succ_key;
        node->values[idx] = succ_val;
        return mel__btree_remove_from_node(bt, node->children[idx + 1], succ_key);
    }
    else
    {
        mel__btree_merge(bt, node, idx);
        return mel__btree_remove_from_node(bt, node->children[idx], key);
    }
}

bool mel__btree_remove_from_node(Mel_BTree* bt, Mel_BTreeNode* node, void* key)
{
    u32 t = bt->degree;
    u32 idx = 0;

    while (idx < node->key_count && bt->cmp(key, node->keys[idx]) > 0)
    {
        idx++;
    }

    if (idx < node->key_count && bt->cmp(key, node->keys[idx]) == 0)
    {
        if (node->leaf)
        {
            mel__btree_remove_from_leaf(node, idx);
            return true;
        }
        return mel__btree_remove_from_internal(bt, node, idx);
    }

    if (node->leaf)
    {
        return false;
    }

    bool last_child = (idx == node->key_count);

    if (node->children[idx]->key_count < t)
    {
        mel__btree_fill(bt, node, idx);
    }

    if (last_child && idx > node->key_count)
    {
        return mel__btree_remove_from_node(bt, node->children[idx - 1], key);
    }
    return mel__btree_remove_from_node(bt, node->children[idx], key);
}

bool mel_btree_remove(Mel_BTree* bt, void* key)
{
    if (!bt->root) return false;

    bool removed = mel__btree_remove_from_node(bt, bt->root, key);

    if (removed)
    {
        bt->count--;

        if (bt->root->key_count == 0)
        {
            Mel_BTreeNode* old_root = bt->root;
            if (bt->root->leaf)
            {
                bt->root = NULL;
            }
            else
            {
                bt->root = bt->root->children[0];
            }
            mel__btree_free_node(bt, old_root);
        }
    }

    return removed;
}
