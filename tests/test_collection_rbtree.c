#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.rbtree.h"

static i32 cmp_i32(const void* a, const void* b)
{
    i32 va = (i32)(isize)a;
    i32 vb = (i32)(isize)b;
    return (va > vb) - (va < vb);
}

#define KEY(v) ((void*)(isize)(v))
#define VAL(v) ((void*)(isize)(v))
#define NODE_KEY(n) ((i32)(isize)(n)->key)
#define NODE_VAL(n) ((i32)(isize)(n)->value)

static bool check_rb_properties(Mel_RBTree* tree, Mel_RBNode* node, i32* black_count, i32 current_black)
{
    Mel_RBNode* nil = &tree->nil_node;

    if (node == nil)
    {
        if (*black_count == -1)
        {
            *black_count = current_black;
        }
        return *black_count == current_black;
    }

    if (node->color == MEL_RB_RED)
    {
        if (node->left->color != MEL_RB_BLACK || node->right->color != MEL_RB_BLACK)
        {
            return false;
        }
    }

    if (node->color == MEL_RB_BLACK)
    {
        current_black++;
    }

    return check_rb_properties(tree, node->left, black_count, current_black)
        && check_rb_properties(tree, node->right, black_count, current_black);
}

static bool verify_rbtree(Mel_RBTree* tree)
{
    Mel_RBNode* nil = &tree->nil_node;

    if (nil->color != MEL_RB_BLACK) return false;

    if (tree->root == nil) return true;

    if (tree->root->color != MEL_RB_BLACK) return false;

    i32 black_count = -1;
    return check_rb_properties(tree, tree->root, &black_count, 0);
}

MEL_TEST(init_empty)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    MEL_ASSERT(mel_rbtree_empty(&tree));
    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)0);
    MEL_ASSERT(mel_rbtree_min(&tree) == &tree.nil_node);
    MEL_ASSERT(mel_rbtree_max(&tree) == &tree.nil_node);
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(insert_single)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    MEL_ASSERT(mel_rbtree_insert(&tree, KEY(42), VAL(100)));
    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)1);

    Mel_RBNode* found = mel_rbtree_find(&tree, KEY(42));
    MEL_ASSERT(found != &tree.nil_node);
    MEL_ASSERT_EQ(NODE_KEY(found), 42);
    MEL_ASSERT_EQ(NODE_VAL(found), 100);
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(insert_multiple_in_order)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    for (i32 i = 1; i <= 10; i++)
    {
        MEL_ASSERT(mel_rbtree_insert(&tree, KEY(i), VAL(i * 10)));
    }

    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)10);
    MEL_ASSERT(verify_rbtree(&tree));

    for (i32 i = 1; i <= 10; i++)
    {
        Mel_RBNode* n = mel_rbtree_find(&tree, KEY(i));
        MEL_ASSERT(n != &tree.nil_node);
        MEL_ASSERT_EQ(NODE_VAL(n), i * 10);
    }

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(insert_reverse_order)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    for (i32 i = 20; i >= 1; i--)
    {
        MEL_ASSERT(mel_rbtree_insert(&tree, KEY(i), VAL(i)));
    }

    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)20);
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(insert_random_order)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    i32 vals[] = { 15, 3, 22, 8, 1, 19, 27, 5, 12, 30, 10, 25, 7, 17, 2 };
    usize n = sizeof(vals) / sizeof(vals[0]);

    for (usize i = 0; i < n; i++)
    {
        MEL_ASSERT(mel_rbtree_insert(&tree, KEY(vals[i]), VAL(vals[i])));
    }

    MEL_ASSERT_EQ(mel_rbtree_count(&tree), n);
    MEL_ASSERT(verify_rbtree(&tree));

    for (usize i = 0; i < n; i++)
    {
        MEL_ASSERT(mel_rbtree_contains(&tree, KEY(vals[i])));
    }

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(find_existing)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(5), VAL(50));
    mel_rbtree_insert(&tree, KEY(3), VAL(30));
    mel_rbtree_insert(&tree, KEY(7), VAL(70));

    Mel_RBNode* n = mel_rbtree_find(&tree, KEY(3));
    MEL_ASSERT(n != &tree.nil_node);
    MEL_ASSERT_EQ(NODE_KEY(n), 3);
    MEL_ASSERT_EQ(NODE_VAL(n), 30);

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(find_nonexisting)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(5), VAL(50));
    mel_rbtree_insert(&tree, KEY(3), VAL(30));
    mel_rbtree_insert(&tree, KEY(7), VAL(70));

    Mel_RBNode* n = mel_rbtree_find(&tree, KEY(99));
    MEL_ASSERT(n == &tree.nil_node);

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(remove_leaf)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(5), VAL(0));
    mel_rbtree_insert(&tree, KEY(3), VAL(0));
    mel_rbtree_insert(&tree, KEY(7), VAL(0));
    mel_rbtree_insert(&tree, KEY(1), VAL(0));

    MEL_ASSERT(mel_rbtree_remove(&tree, KEY(1)));
    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)3);
    MEL_ASSERT(!mel_rbtree_contains(&tree, KEY(1)));
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(remove_node_one_child)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(10), VAL(0));
    mel_rbtree_insert(&tree, KEY(5), VAL(0));
    mel_rbtree_insert(&tree, KEY(15), VAL(0));
    mel_rbtree_insert(&tree, KEY(12), VAL(0));

    MEL_ASSERT(mel_rbtree_remove(&tree, KEY(15)));
    MEL_ASSERT(!mel_rbtree_contains(&tree, KEY(15)));
    MEL_ASSERT(mel_rbtree_contains(&tree, KEY(12)));
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(remove_node_two_children)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(10), VAL(0));
    mel_rbtree_insert(&tree, KEY(5), VAL(0));
    mel_rbtree_insert(&tree, KEY(15), VAL(0));
    mel_rbtree_insert(&tree, KEY(3), VAL(0));
    mel_rbtree_insert(&tree, KEY(7), VAL(0));

    MEL_ASSERT(mel_rbtree_remove(&tree, KEY(5)));
    MEL_ASSERT(!mel_rbtree_contains(&tree, KEY(5)));
    MEL_ASSERT(mel_rbtree_contains(&tree, KEY(3)));
    MEL_ASSERT(mel_rbtree_contains(&tree, KEY(7)));
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(remove_root)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(10), VAL(0));
    mel_rbtree_insert(&tree, KEY(5), VAL(0));
    mel_rbtree_insert(&tree, KEY(15), VAL(0));

    MEL_ASSERT(mel_rbtree_remove(&tree, KEY(10)));
    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)2);
    MEL_ASSERT(!mel_rbtree_contains(&tree, KEY(10)));
    MEL_ASSERT(mel_rbtree_contains(&tree, KEY(5)));
    MEL_ASSERT(mel_rbtree_contains(&tree, KEY(15)));
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(remove_single_element)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(42), VAL(0));
    MEL_ASSERT(mel_rbtree_remove(&tree, KEY(42)));
    MEL_ASSERT(mel_rbtree_empty(&tree));
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(min_max)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(10), VAL(0));
    mel_rbtree_insert(&tree, KEY(5), VAL(0));
    mel_rbtree_insert(&tree, KEY(15), VAL(0));
    mel_rbtree_insert(&tree, KEY(1), VAL(0));
    mel_rbtree_insert(&tree, KEY(20), VAL(0));

    MEL_ASSERT_EQ(NODE_KEY(mel_rbtree_min(&tree)), 1);
    MEL_ASSERT_EQ(NODE_KEY(mel_rbtree_max(&tree)), 20);

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(in_order_traversal)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    i32 vals[] = { 15, 3, 22, 8, 1, 19, 27, 5, 12, 30 };
    usize n = sizeof(vals) / sizeof(vals[0]);

    for (usize i = 0; i < n; i++)
    {
        mel_rbtree_insert(&tree, KEY(vals[i]), VAL(vals[i]));
    }

    i32 expected[] = { 1, 3, 5, 8, 12, 15, 19, 22, 27, 30 };
    usize idx = 0;
    Mel_RBNode* node = mel_rbtree_min(&tree);
    while (node != &tree.nil_node)
    {
        MEL_ASSERT_LT(idx, n);
        MEL_ASSERT_EQ(NODE_KEY(node), expected[idx]);
        node = mel_rbtree_next(&tree, node);
        idx++;
    }
    MEL_ASSERT_EQ(idx, n);

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(reverse_traversal)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    i32 vals[] = { 15, 3, 22, 8, 1, 19, 27, 5, 12, 30 };
    usize n = sizeof(vals) / sizeof(vals[0]);

    for (usize i = 0; i < n; i++)
    {
        mel_rbtree_insert(&tree, KEY(vals[i]), VAL(vals[i]));
    }

    i32 expected[] = { 30, 27, 22, 19, 15, 12, 8, 5, 3, 1 };
    usize idx = 0;
    Mel_RBNode* node = mel_rbtree_max(&tree);
    while (node != &tree.nil_node)
    {
        MEL_ASSERT_LT(idx, n);
        MEL_ASSERT_EQ(NODE_KEY(node), expected[idx]);
        node = mel_rbtree_prev(&tree, node);
        idx++;
    }
    MEL_ASSERT_EQ(idx, n);

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(clear)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    for (i32 i = 0; i < 10; i++)
    {
        mel_rbtree_insert(&tree, KEY(i), VAL(i));
    }

    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)10);
    mel_rbtree_clear(&tree);
    MEL_ASSERT(mel_rbtree_empty(&tree));
    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)0);
    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(contains)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(10), VAL(0));
    mel_rbtree_insert(&tree, KEY(20), VAL(0));
    mel_rbtree_insert(&tree, KEY(30), VAL(0));

    MEL_ASSERT(mel_rbtree_contains(&tree, KEY(10)));
    MEL_ASSERT(mel_rbtree_contains(&tree, KEY(20)));
    MEL_ASSERT(mel_rbtree_contains(&tree, KEY(30)));
    MEL_ASSERT(!mel_rbtree_contains(&tree, KEY(5)));
    MEL_ASSERT(!mel_rbtree_contains(&tree, KEY(15)));
    MEL_ASSERT(!mel_rbtree_contains(&tree, KEY(99)));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(stress)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    for (i32 i = 0; i < 100; i++)
    {
        MEL_ASSERT(mel_rbtree_insert(&tree, KEY(i), VAL(i * 7)));
    }

    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)100);
    MEL_ASSERT(verify_rbtree(&tree));

    for (i32 i = 0; i < 100; i++)
    {
        Mel_RBNode* n = mel_rbtree_find(&tree, KEY(i));
        MEL_ASSERT(n != &tree.nil_node);
        MEL_ASSERT_EQ(NODE_VAL(n), i * 7);
    }

    for (i32 i = 0; i < 100; i += 2)
    {
        MEL_ASSERT(mel_rbtree_remove(&tree, KEY(i)));
    }

    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)50);
    MEL_ASSERT(verify_rbtree(&tree));

    for (i32 i = 0; i < 100; i++)
    {
        bool found = mel_rbtree_contains(&tree, KEY(i));
        if (i % 2 == 0)
        {
            MEL_ASSERT(!found);
        }
        else
        {
            MEL_ASSERT(found);
        }
    }

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(insert_duplicate)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    MEL_ASSERT(mel_rbtree_insert(&tree, KEY(10), VAL(100)));
    MEL_ASSERT(!mel_rbtree_insert(&tree, KEY(10), VAL(200)));
    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)1);

    Mel_RBNode* n = mel_rbtree_find(&tree, KEY(10));
    MEL_ASSERT_EQ(NODE_VAL(n), 100);

    MEL_ASSERT(verify_rbtree(&tree));

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(rb_properties)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    for (i32 i = 1; i <= 31; i++)
    {
        mel_rbtree_insert(&tree, KEY(i), VAL(0));
        MEL_ASSERT(verify_rbtree(&tree));
    }

    for (i32 i = 1; i <= 15; i++)
    {
        mel_rbtree_remove(&tree, KEY(i * 2));
        MEL_ASSERT(verify_rbtree(&tree));
    }

    for (i32 i = 31; i >= 16; i--)
    {
        mel_rbtree_remove(&tree, KEY(i));
        MEL_ASSERT(verify_rbtree(&tree));
    }

    mel_rbtree_free(&tree);
    MEL_PASS();
}

MEL_TEST(remove_nonexistent)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_RBTree tree;
    mel_rbtree_init(&tree, cmp_i32, alloc);

    mel_rbtree_insert(&tree, KEY(5), VAL(0));
    MEL_ASSERT(!mel_rbtree_remove(&tree, KEY(99)));
    MEL_ASSERT_EQ(mel_rbtree_count(&tree), (usize)1);

    mel_rbtree_free(&tree);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Red-Black Tree Tests");

    MEL_RUN_TEST(init_empty);
    MEL_RUN_TEST(insert_single);
    MEL_RUN_TEST(insert_multiple_in_order);
    MEL_RUN_TEST(insert_reverse_order);
    MEL_RUN_TEST(insert_random_order);
    MEL_RUN_TEST(find_existing);
    MEL_RUN_TEST(find_nonexisting);
    MEL_RUN_TEST(remove_leaf);
    MEL_RUN_TEST(remove_node_one_child);
    MEL_RUN_TEST(remove_node_two_children);
    MEL_RUN_TEST(remove_root);
    MEL_RUN_TEST(remove_single_element);
    MEL_RUN_TEST(min_max);
    MEL_RUN_TEST(in_order_traversal);
    MEL_RUN_TEST(reverse_traversal);
    MEL_RUN_TEST(clear);
    MEL_RUN_TEST(contains);
    MEL_RUN_TEST(stress);
    MEL_RUN_TEST(insert_duplicate);
    MEL_RUN_TEST(rb_properties);
    MEL_RUN_TEST(remove_nonexistent);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
