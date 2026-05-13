#include "rbtree.h"
#include "allocator.h"

static Mel_RBNode* mel__rbtree_nil(Mel_RBTree* tree)
{
    return &tree->nil_node;
}

static Mel_RBNode* mel__rbtree_alloc_node(Mel_RBTree* tree, void* key, void* value)
{
    Mel_RBNode* node = mel_alloc_type(tree->allocator, Mel_RBNode);
    node->left = mel__rbtree_nil(tree);
    node->right = mel__rbtree_nil(tree);
    node->parent = mel__rbtree_nil(tree);
    node->key = key;
    node->value = value;
    node->color = MEL_RB_RED;
    return node;
}

static void mel__rbtree_rotate_left(Mel_RBTree* tree, Mel_RBNode* x)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    Mel_RBNode* y = x->right;
    x->right = y->left;

    if (y->left != nil)
    {
        y->left->parent = x;
    }

    y->parent = x->parent;

    if (x->parent == nil)
    {
        tree->root = y;
    }
    else if (x == x->parent->left)
    {
        x->parent->left = y;
    }
    else
    {
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;
}

static void mel__rbtree_rotate_right(Mel_RBTree* tree, Mel_RBNode* x)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    Mel_RBNode* y = x->left;
    x->left = y->right;

    if (y->right != nil)
    {
        y->right->parent = x;
    }

    y->parent = x->parent;

    if (x->parent == nil)
    {
        tree->root = y;
    }
    else if (x == x->parent->right)
    {
        x->parent->right = y;
    }
    else
    {
        x->parent->left = y;
    }

    y->right = x;
    x->parent = y;
}

static void mel__rbtree_insert_fixup(Mel_RBTree* tree, Mel_RBNode* z)
{
    while (z->parent->color == MEL_RB_RED)
    {
        if (z->parent == z->parent->parent->left)
        {
            Mel_RBNode* y = z->parent->parent->right;
            if (y->color == MEL_RB_RED)
            {
                z->parent->color = MEL_RB_BLACK;
                y->color = MEL_RB_BLACK;
                z->parent->parent->color = MEL_RB_RED;
                z = z->parent->parent;
            }
            else
            {
                if (z == z->parent->right)
                {
                    z = z->parent;
                    mel__rbtree_rotate_left(tree, z);
                }
                z->parent->color = MEL_RB_BLACK;
                z->parent->parent->color = MEL_RB_RED;
                mel__rbtree_rotate_right(tree, z->parent->parent);
            }
        }
        else
        {
            Mel_RBNode* y = z->parent->parent->left;
            if (y->color == MEL_RB_RED)
            {
                z->parent->color = MEL_RB_BLACK;
                y->color = MEL_RB_BLACK;
                z->parent->parent->color = MEL_RB_RED;
                z = z->parent->parent;
            }
            else
            {
                if (z == z->parent->left)
                {
                    z = z->parent;
                    mel__rbtree_rotate_right(tree, z);
                }
                z->parent->color = MEL_RB_BLACK;
                z->parent->parent->color = MEL_RB_RED;
                mel__rbtree_rotate_left(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = MEL_RB_BLACK;
}

static void mel__rbtree_transplant(Mel_RBTree* tree, Mel_RBNode* u, Mel_RBNode* v)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);

    if (u->parent == nil)
    {
        tree->root = v;
    }
    else if (u == u->parent->left)
    {
        u->parent->left = v;
    }
    else
    {
        u->parent->right = v;
    }
    v->parent = u->parent;
}

static Mel_RBNode* mel__rbtree_subtree_min(Mel_RBTree* tree, Mel_RBNode* node)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    while (node->left != nil)
    {
        node = node->left;
    }
    return node;
}

static Mel_RBNode* mel__rbtree_subtree_max(Mel_RBTree* tree, Mel_RBNode* node)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    while (node->right != nil)
    {
        node = node->right;
    }
    return node;
}

static void mel__rbtree_delete_fixup(Mel_RBTree* tree, Mel_RBNode* x)
{
    while (x != tree->root && x->color == MEL_RB_BLACK)
    {
        if (x == x->parent->left)
        {
            Mel_RBNode* w = x->parent->right;

            if (w->color == MEL_RB_RED)
            {
                w->color = MEL_RB_BLACK;
                x->parent->color = MEL_RB_RED;
                mel__rbtree_rotate_left(tree, x->parent);
                w = x->parent->right;
            }

            if (w->left->color == MEL_RB_BLACK && w->right->color == MEL_RB_BLACK)
            {
                w->color = MEL_RB_RED;
                x = x->parent;
            }
            else
            {
                if (w->right->color == MEL_RB_BLACK)
                {
                    w->left->color = MEL_RB_BLACK;
                    w->color = MEL_RB_RED;
                    mel__rbtree_rotate_right(tree, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = MEL_RB_BLACK;
                w->right->color = MEL_RB_BLACK;
                mel__rbtree_rotate_left(tree, x->parent);
                x = tree->root;
            }
        }
        else
        {
            Mel_RBNode* w = x->parent->left;

            if (w->color == MEL_RB_RED)
            {
                w->color = MEL_RB_BLACK;
                x->parent->color = MEL_RB_RED;
                mel__rbtree_rotate_right(tree, x->parent);
                w = x->parent->left;
            }

            if (w->right->color == MEL_RB_BLACK && w->left->color == MEL_RB_BLACK)
            {
                w->color = MEL_RB_RED;
                x = x->parent;
            }
            else
            {
                if (w->left->color == MEL_RB_BLACK)
                {
                    w->right->color = MEL_RB_BLACK;
                    w->color = MEL_RB_RED;
                    mel__rbtree_rotate_left(tree, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = MEL_RB_BLACK;
                w->left->color = MEL_RB_BLACK;
                mel__rbtree_rotate_right(tree, x->parent);
                x = tree->root;
            }
        }
    }
    x->color = MEL_RB_BLACK;
}

static void mel__rbtree_free_subtree(Mel_RBTree* tree, Mel_RBNode* node)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    if (node == nil) return;
    mel__rbtree_free_subtree(tree, node->left);
    mel__rbtree_free_subtree(tree, node->right);
    mel_dealloc(tree->allocator, node);
}

void mel_rbtree_init(Mel_RBTree* tree, Mel_RBTree_Cmp cmp, const Mel_Alloc* alloc)
{
    tree->nil_node.left = nullptr;
    tree->nil_node.right = nullptr;
    tree->nil_node.parent = nullptr;
    tree->nil_node.key = nullptr;
    tree->nil_node.value = nullptr;
    tree->nil_node.color = MEL_RB_BLACK;
    tree->root = &tree->nil_node;
    tree->count = 0;
    tree->cmp = cmp;
    tree->allocator = alloc;
}

void mel_rbtree_free(Mel_RBTree* tree)
{
    mel__rbtree_free_subtree(tree, tree->root);
    tree->root = &tree->nil_node;
    tree->count = 0;
}

bool mel_rbtree_insert(Mel_RBTree* tree, void* key, void* value)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    Mel_RBNode* y = nil;
    Mel_RBNode* x = tree->root;

    while (x != nil)
    {
        y = x;
        i32 c = tree->cmp(key, x->key);
        if (c < 0)
        {
            x = x->left;
        }
        else if (c > 0)
        {
            x = x->right;
        }
        else
        {
            return false;
        }
    }

    Mel_RBNode* z = mel__rbtree_alloc_node(tree, key, value);
    z->parent = y;

    if (y == nil)
    {
        tree->root = z;
    }
    else if (tree->cmp(z->key, y->key) < 0)
    {
        y->left = z;
    }
    else
    {
        y->right = z;
    }

    mel__rbtree_insert_fixup(tree, z);
    tree->count++;
    return true;
}

Mel_RBNode* mel_rbtree_find(Mel_RBTree* tree, void* key)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    Mel_RBNode* x = tree->root;

    while (x != nil)
    {
        i32 c = tree->cmp(key, x->key);
        if (c < 0)
        {
            x = x->left;
        }
        else if (c > 0)
        {
            x = x->right;
        }
        else
        {
            return x;
        }
    }
    return nil;
}

bool mel_rbtree_remove(Mel_RBTree* tree, void* key)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    Mel_RBNode* z = mel_rbtree_find(tree, key);
    if (z == nil) return false;

    Mel_RBNode* y = z;
    Mel_RBNode* x;
    u8 y_orig_color = y->color;

    if (z->left == nil)
    {
        x = z->right;
        mel__rbtree_transplant(tree, z, z->right);
    }
    else if (z->right == nil)
    {
        x = z->left;
        mel__rbtree_transplant(tree, z, z->left);
    }
    else
    {
        y = mel__rbtree_subtree_min(tree, z->right);
        y_orig_color = y->color;
        x = y->right;

        if (y->parent == z)
        {
            x->parent = y;
        }
        else
        {
            mel__rbtree_transplant(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }

        mel__rbtree_transplant(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    mel_dealloc(tree->allocator, z);
    tree->count--;

    if (y_orig_color == MEL_RB_BLACK)
    {
        mel__rbtree_delete_fixup(tree, x);
    }

    return true;
}

Mel_RBNode* mel_rbtree_min(Mel_RBTree* tree)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    if (tree->root == nil) return nil;
    return mel__rbtree_subtree_min(tree, tree->root);
}

Mel_RBNode* mel_rbtree_max(Mel_RBTree* tree)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    if (tree->root == nil) return nil;
    return mel__rbtree_subtree_max(tree, tree->root);
}

Mel_RBNode* mel_rbtree_next(Mel_RBTree* tree, Mel_RBNode* node)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    if (node == nil) return nil;

    if (node->right != nil)
    {
        return mel__rbtree_subtree_min(tree, node->right);
    }

    Mel_RBNode* y = node->parent;
    while (y != nil && node == y->right)
    {
        node = y;
        y = y->parent;
    }
    return y;
}

Mel_RBNode* mel_rbtree_prev(Mel_RBTree* tree, Mel_RBNode* node)
{
    Mel_RBNode* nil = mel__rbtree_nil(tree);
    if (node == nil) return nil;

    if (node->left != nil)
    {
        return mel__rbtree_subtree_max(tree, node->left);
    }

    Mel_RBNode* y = node->parent;
    while (y != nil && node == y->left)
    {
        node = y;
        y = y->parent;
    }
    return y;
}

usize mel_rbtree_count(Mel_RBTree* tree)
{
    return tree->count;
}

bool mel_rbtree_empty(Mel_RBTree* tree)
{
    return tree->count == 0;
}

void mel_rbtree_clear(Mel_RBTree* tree)
{
    mel__rbtree_free_subtree(tree, tree->root);
    tree->root = &tree->nil_node;
    tree->count = 0;
}

bool mel_rbtree_contains(Mel_RBTree* tree, void* key)
{
    return mel_rbtree_find(tree, key) != &tree->nil_node;
}
