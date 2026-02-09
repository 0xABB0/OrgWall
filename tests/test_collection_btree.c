#include "../melody/test.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.btree.h"

static i32 cmp_i32(const void* a, const void* b)
{
    isize va = (isize)a;
    isize vb = (isize)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

#define KEY(x)   ((void*)(isize)(x))
#define VAL(x)   ((void*)(isize)(x))
#define UNVAL(v) ((i32)(isize)(v))

MEL_TEST(init_empty)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)0);
    MEL_ASSERT(mel_btree_empty(&bt));
    MEL_ASSERT_NULL(mel_btree_find(&bt, KEY(42)));
    MEL_ASSERT_NULL(mel_btree_min(&bt));
    MEL_ASSERT_NULL(mel_btree_max(&bt));
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(insert_single_find)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    MEL_ASSERT(mel_btree_insert(&bt, KEY(10), VAL(100)));
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)1);
    MEL_ASSERT(!mel_btree_empty(&bt));
    void* v = mel_btree_find(&bt, KEY(10));
    MEL_ASSERT_NOT_NULL(v);
    MEL_ASSERT_EQ(UNVAL(v), 100);
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(insert_duplicate_rejected)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    MEL_ASSERT(mel_btree_insert(&bt, KEY(5), VAL(50)));
    MEL_ASSERT(!mel_btree_insert(&bt, KEY(5), VAL(99)));
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)1);
    MEL_ASSERT_EQ(UNVAL(mel_btree_find(&bt, KEY(5))), 50);
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(insert_ascending_find_all)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    for (i32 i = 1; i <= 20; i++)
    {
        MEL_ASSERT(mel_btree_insert(&bt, KEY(i), VAL(i * 10)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)20);
    for (i32 i = 1; i <= 20; i++)
    {
        void* v = mel_btree_find(&bt, KEY(i));
        MEL_ASSERT_NOT_NULL(v);
        MEL_ASSERT_EQ(UNVAL(v), i * 10);
    }
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(insert_descending_triggers_splits)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    for (i32 i = 20; i >= 1; i--)
    {
        MEL_ASSERT(mel_btree_insert(&bt, KEY(i), VAL(i)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)20);
    for (i32 i = 1; i <= 20; i++)
    {
        MEL_ASSERT(mel_btree_contains(&bt, KEY(i)));
    }
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(insert_causes_root_split)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    for (i32 i = 1; i <= 6; i++)
    {
        MEL_ASSERT(mel_btree_insert(&bt, KEY(i), VAL(i)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)6);
    MEL_ASSERT(!bt.root->leaf);
    for (i32 i = 1; i <= 6; i++)
    {
        MEL_ASSERT(mel_btree_contains(&bt, KEY(i)));
    }
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(find_nonexistent)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    for (i32 i = 0; i < 10; i++)
    {
        mel_btree_insert(&bt, KEY(i * 2), VAL(i));
    }
    MEL_ASSERT_NULL(mel_btree_find(&bt, KEY(1)));
    MEL_ASSERT_NULL(mel_btree_find(&bt, KEY(3)));
    MEL_ASSERT_NULL(mel_btree_find(&bt, KEY(99)));
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(remove_from_leaf)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    mel_btree_insert(&bt, KEY(1), VAL(10));
    mel_btree_insert(&bt, KEY(2), VAL(20));
    mel_btree_insert(&bt, KEY(3), VAL(30));
    MEL_ASSERT(mel_btree_remove(&bt, KEY(1)));
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)2);
    MEL_ASSERT(!mel_btree_contains(&bt, KEY(1)));
    MEL_ASSERT(mel_btree_contains(&bt, KEY(2)));
    MEL_ASSERT(mel_btree_contains(&bt, KEY(3)));
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(remove_nonexistent)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    mel_btree_insert(&bt, KEY(5), VAL(50));
    MEL_ASSERT(!mel_btree_remove(&bt, KEY(99)));
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)1);
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(remove_from_internal)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    for (i32 i = 1; i <= 10; i++)
    {
        mel_btree_insert(&bt, KEY(i), VAL(i * 10));
    }

    MEL_ASSERT(mel_btree_remove(&bt, KEY(3)));
    MEL_ASSERT(!mel_btree_contains(&bt, KEY(3)));
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)9);

    for (i32 i = 1; i <= 10; i++)
    {
        if (i == 3) continue;
        MEL_ASSERT(mel_btree_contains(&bt, KEY(i)));
    }
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(remove_causing_merge)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    for (i32 i = 1; i <= 10; i++)
    {
        mel_btree_insert(&bt, KEY(i), VAL(i));
    }

    for (i32 i = 1; i <= 7; i++)
    {
        MEL_ASSERT(mel_btree_remove(&bt, KEY(i)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)3);
    MEL_ASSERT(mel_btree_contains(&bt, KEY(8)));
    MEL_ASSERT(mel_btree_contains(&bt, KEY(9)));
    MEL_ASSERT(mel_btree_contains(&bt, KEY(10)));
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(remove_causing_borrow)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    for (i32 i = 1; i <= 15; i++)
    {
        mel_btree_insert(&bt, KEY(i), VAL(i));
    }

    MEL_ASSERT(mel_btree_remove(&bt, KEY(1)));
    MEL_ASSERT(!mel_btree_contains(&bt, KEY(1)));

    for (i32 i = 2; i <= 15; i++)
    {
        MEL_ASSERT(mel_btree_contains(&bt, KEY(i)));
    }
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(min_max)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    mel_btree_insert(&bt, KEY(50), VAL(1));
    mel_btree_insert(&bt, KEY(10), VAL(2));
    mel_btree_insert(&bt, KEY(90), VAL(3));
    mel_btree_insert(&bt, KEY(30), VAL(4));
    mel_btree_insert(&bt, KEY(70), VAL(5));

    MEL_ASSERT_EQ((isize)mel_btree_min(&bt), (isize)10);
    MEL_ASSERT_EQ((isize)mel_btree_max(&bt), (isize)90);
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(contains)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    mel_btree_insert(&bt, KEY(42), VAL(1));
    MEL_ASSERT(mel_btree_contains(&bt, KEY(42)));
    MEL_ASSERT(!mel_btree_contains(&bt, KEY(43)));
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(clear)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    for (i32 i = 0; i < 20; i++)
    {
        mel_btree_insert(&bt, KEY(i), VAL(i));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)20);
    mel_btree_clear(&bt);
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)0);
    MEL_ASSERT(mel_btree_empty(&bt));
    MEL_ASSERT_NULL(mel_btree_find(&bt, KEY(0)));
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(count)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)0);
    mel_btree_insert(&bt, KEY(1), VAL(1));
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)1);
    mel_btree_insert(&bt, KEY(2), VAL(2));
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)2);
    mel_btree_remove(&bt, KEY(1));
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)1);
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(stress_200)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());

    for (i32 i = 1; i <= 200; i++)
    {
        MEL_ASSERT(mel_btree_insert(&bt, KEY(i), VAL(i * 3)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)200);

    for (i32 i = 1; i <= 200; i++)
    {
        void* v = mel_btree_find(&bt, KEY(i));
        MEL_ASSERT_NOT_NULL(v);
        MEL_ASSERT_EQ(UNVAL(v), i * 3);
    }

    for (i32 i = 1; i <= 200; i++)
    {
        MEL_ASSERT(mel_btree_remove(&bt, KEY(i)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)0);
    MEL_ASSERT(mel_btree_empty(&bt));
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(degree_2)
{
    Mel_BTree bt;
    mel_btree_init(&bt, 2, cmp_i32, mel_alloc_heap());

    for (i32 i = 1; i <= 30; i++)
    {
        MEL_ASSERT(mel_btree_insert(&bt, KEY(i), VAL(i)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)30);

    for (i32 i = 1; i <= 30; i++)
    {
        MEL_ASSERT(mel_btree_contains(&bt, KEY(i)));
    }

    MEL_ASSERT_EQ((isize)mel_btree_min(&bt), (isize)1);
    MEL_ASSERT_EQ((isize)mel_btree_max(&bt), (isize)30);

    for (i32 i = 1; i <= 30; i++)
    {
        MEL_ASSERT(mel_btree_remove(&bt, KEY(i)));
    }
    MEL_ASSERT(mel_btree_empty(&bt));
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(degree_5)
{
    Mel_BTree bt;
    mel_btree_init(&bt, 5, cmp_i32, mel_alloc_heap());

    for (i32 i = 50; i >= 1; i--)
    {
        MEL_ASSERT(mel_btree_insert(&bt, KEY(i), VAL(i * 2)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)50);

    for (i32 i = 1; i <= 50; i++)
    {
        void* v = mel_btree_find(&bt, KEY(i));
        MEL_ASSERT_NOT_NULL(v);
        MEL_ASSERT_EQ(UNVAL(v), i * 2);
    }

    MEL_ASSERT_EQ((isize)mel_btree_min(&bt), (isize)1);
    MEL_ASSERT_EQ((isize)mel_btree_max(&bt), (isize)50);

    for (i32 i = 25; i >= 1; i--)
    {
        MEL_ASSERT(mel_btree_remove(&bt, KEY(i)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)25);

    for (i32 i = 26; i <= 50; i++)
    {
        MEL_ASSERT(mel_btree_contains(&bt, KEY(i)));
    }
    mel_btree_free(&bt);
    MEL_PASS();
}

MEL_TEST(remove_all_then_reinsert)
{
    Mel_BTree bt;
    mel_btree_init(&bt, MEL_BTREE_DEFAULT_DEGREE, cmp_i32, mel_alloc_heap());

    for (i32 i = 0; i < 10; i++)
    {
        mel_btree_insert(&bt, KEY(i), VAL(i));
    }
    for (i32 i = 0; i < 10; i++)
    {
        mel_btree_remove(&bt, KEY(i));
    }
    MEL_ASSERT(mel_btree_empty(&bt));
    MEL_ASSERT_NULL(bt.root);

    for (i32 i = 0; i < 10; i++)
    {
        MEL_ASSERT(mel_btree_insert(&bt, KEY(i), VAL(i * 5)));
    }
    MEL_ASSERT_EQ(mel_btree_count(&bt), (usize)10);
    for (i32 i = 0; i < 10; i++)
    {
        MEL_ASSERT_EQ(UNVAL(mel_btree_find(&bt, KEY(i))), i * 5);
    }
    mel_btree_free(&bt);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("B-Tree Tests");

    MEL_RUN_TEST(init_empty);
    MEL_RUN_TEST(insert_single_find);
    MEL_RUN_TEST(insert_duplicate_rejected);
    MEL_RUN_TEST(insert_ascending_find_all);
    MEL_RUN_TEST(insert_descending_triggers_splits);
    MEL_RUN_TEST(insert_causes_root_split);
    MEL_RUN_TEST(find_nonexistent);
    MEL_RUN_TEST(remove_from_leaf);
    MEL_RUN_TEST(remove_nonexistent);
    MEL_RUN_TEST(remove_from_internal);
    MEL_RUN_TEST(remove_causing_merge);
    MEL_RUN_TEST(remove_causing_borrow);
    MEL_RUN_TEST(min_max);
    MEL_RUN_TEST(contains);
    MEL_RUN_TEST(clear);
    MEL_RUN_TEST(count);
    MEL_RUN_TEST(stress_200);
    MEL_RUN_TEST(degree_2);
    MEL_RUN_TEST(degree_5);
    MEL_RUN_TEST(remove_all_then_reinsert);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
