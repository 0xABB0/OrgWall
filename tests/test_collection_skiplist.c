#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.skiplist.h"

static i32 cmp_i32(const void* a, const void* b)
{
    isize va = (isize)a;
    isize vb = (isize)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

MEL_TEST(init_empty, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());
    MEL_ASSERT(mel_skiplist_empty(&sl));
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)0);
    MEL_ASSERT_NULL(mel_skiplist_min(&sl));
    MEL_ASSERT_NULL(mel_skiplist_max(&sl));
    mel_skiplist_free(&sl);
}

MEL_TEST(insert_single_find, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    bool ok = mel_skiplist_insert(&sl, (void*)(isize)42, (void*)(isize)100);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)1);

    Mel_SkipNode* n = mel_skiplist_find(&sl, (void*)(isize)42);
    MEL_ASSERT_NOT_NULL(n);
    MEL_ASSERT_EQ((isize)n->key, (isize)42);
    MEL_ASSERT_EQ((isize)n->value, (isize)100);

    mel_skiplist_free(&sl);
}

MEL_TEST(insert_multiple_in_order, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    for (isize i = 0; i < 10; i++)
    {
        mel_skiplist_insert(&sl, (void*)i, (void*)(i * 10));
    }
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)10);

    for (isize i = 0; i < 10; i++)
    {
        Mel_SkipNode* n = mel_skiplist_find(&sl, (void*)i);
        MEL_ASSERT_NOT_NULL(n);
        MEL_ASSERT_EQ((isize)n->value, i * 10);
    }

    mel_skiplist_free(&sl);
}

MEL_TEST(insert_reverse_order, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    for (isize i = 9; i >= 0; i--)
    {
        mel_skiplist_insert(&sl, (void*)i, (void*)(i * 10));
    }
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)10);

    Mel_SkipNode* curr = sl.header->forward[0];
    isize prev = -1;
    while (curr)
    {
        MEL_ASSERT_GT((isize)curr->key, prev);
        prev = (isize)curr->key;
        curr = curr->forward[0];
    }

    mel_skiplist_free(&sl);
}

MEL_TEST(find_existing_and_missing, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    mel_skiplist_insert(&sl, (void*)(isize)5, (void*)(isize)50);
    mel_skiplist_insert(&sl, (void*)(isize)10, (void*)(isize)100);

    MEL_ASSERT_NOT_NULL(mel_skiplist_find(&sl, (void*)(isize)5));
    MEL_ASSERT_NOT_NULL(mel_skiplist_find(&sl, (void*)(isize)10));
    MEL_ASSERT_NULL(mel_skiplist_find(&sl, (void*)(isize)1));
    MEL_ASSERT_NULL(mel_skiplist_find(&sl, (void*)(isize)7));
    MEL_ASSERT_NULL(mel_skiplist_find(&sl, (void*)(isize)99));

    mel_skiplist_free(&sl);
}

MEL_TEST(remove_single, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    mel_skiplist_insert(&sl, (void*)(isize)42, (void*)(isize)100);
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)1);

    bool ok = mel_skiplist_remove(&sl, (void*)(isize)42);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)0);
    MEL_ASSERT_NULL(mel_skiplist_find(&sl, (void*)(isize)42));

    mel_skiplist_free(&sl);
}

MEL_TEST(remove_nonexistent, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    mel_skiplist_insert(&sl, (void*)(isize)1, (void*)(isize)10);
    bool ok = mel_skiplist_remove(&sl, (void*)(isize)999);
    MEL_ASSERT(!ok);
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)1);

    mel_skiplist_free(&sl);
}

MEL_TEST(remove_middle, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    mel_skiplist_insert(&sl, (void*)(isize)1, (void*)(isize)10);
    mel_skiplist_insert(&sl, (void*)(isize)2, (void*)(isize)20);
    mel_skiplist_insert(&sl, (void*)(isize)3, (void*)(isize)30);

    mel_skiplist_remove(&sl, (void*)(isize)2);
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)2);
    MEL_ASSERT_NULL(mel_skiplist_find(&sl, (void*)(isize)2));
    MEL_ASSERT_NOT_NULL(mel_skiplist_find(&sl, (void*)(isize)1));
    MEL_ASSERT_NOT_NULL(mel_skiplist_find(&sl, (void*)(isize)3));

    mel_skiplist_free(&sl);
}

MEL_TEST(min_max, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    mel_skiplist_insert(&sl, (void*)(isize)50, (void*)(isize)0);
    mel_skiplist_insert(&sl, (void*)(isize)10, (void*)(isize)0);
    mel_skiplist_insert(&sl, (void*)(isize)90, (void*)(isize)0);
    mel_skiplist_insert(&sl, (void*)(isize)30, (void*)(isize)0);
    mel_skiplist_insert(&sl, (void*)(isize)70, (void*)(isize)0);

    Mel_SkipNode* min = mel_skiplist_min(&sl);
    Mel_SkipNode* max = mel_skiplist_max(&sl);
    MEL_ASSERT_NOT_NULL(min);
    MEL_ASSERT_NOT_NULL(max);
    MEL_ASSERT_EQ((isize)min->key, (isize)10);
    MEL_ASSERT_EQ((isize)max->key, (isize)90);

    mel_skiplist_free(&sl);
}

MEL_TEST(in_order_traversal, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    isize values[] = {50, 20, 80, 10, 40, 60, 90, 30, 70};
    for (usize i = 0; i < sizeof(values) / sizeof(values[0]); i++)
    {
        mel_skiplist_insert(&sl, (void*)values[i], (void*)(values[i] * 10));
    }

    Mel_SkipNode* curr = sl.header->forward[0];
    isize prev = -1;
    usize traversed = 0;
    while (curr)
    {
        MEL_ASSERT_GT((isize)curr->key, prev);
        prev = (isize)curr->key;
        traversed++;
        curr = curr->forward[0];
    }
    MEL_ASSERT_EQ(traversed, (usize)9);

    mel_skiplist_free(&sl);
}

MEL_TEST(contains, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    mel_skiplist_insert(&sl, (void*)(isize)5, (void*)(isize)50);
    mel_skiplist_insert(&sl, (void*)(isize)15, (void*)(isize)150);

    MEL_ASSERT(mel_skiplist_contains(&sl, (void*)(isize)5));
    MEL_ASSERT(mel_skiplist_contains(&sl, (void*)(isize)15));
    MEL_ASSERT(!mel_skiplist_contains(&sl, (void*)(isize)10));

    mel_skiplist_free(&sl);
}

MEL_TEST(clear, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    for (isize i = 0; i < 20; i++)
    {
        mel_skiplist_insert(&sl, (void*)i, (void*)(i * 10));
    }
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)20);

    mel_skiplist_clear(&sl);
    MEL_ASSERT(mel_skiplist_empty(&sl));
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)0);
    MEL_ASSERT_NULL(mel_skiplist_min(&sl));
    MEL_ASSERT_NULL(mel_skiplist_max(&sl));

    mel_skiplist_insert(&sl, (void*)(isize)99, (void*)(isize)990);
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)1);
    MEL_ASSERT_NOT_NULL(mel_skiplist_find(&sl, (void*)(isize)99));

    mel_skiplist_free(&sl);
}

MEL_TEST(stress, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    for (isize i = 0; i < 100; i++)
    {
        mel_skiplist_insert(&sl, (void*)i, (void*)(i * 7));
    }
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)100);

    for (isize i = 0; i < 100; i++)
    {
        Mel_SkipNode* n = mel_skiplist_find(&sl, (void*)i);
        MEL_ASSERT_NOT_NULL(n);
        MEL_ASSERT_EQ((isize)n->value, i * 7);
    }

    for (isize i = 0; i < 100; i += 2)
    {
        bool ok = mel_skiplist_remove(&sl, (void*)i);
        MEL_ASSERT(ok);
    }
    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)50);

    for (isize i = 0; i < 100; i++)
    {
        Mel_SkipNode* n = mel_skiplist_find(&sl, (void*)i);
        if (i % 2 == 0)
        {
            MEL_ASSERT_NULL(n);
        }
        else
        {
            MEL_ASSERT_NOT_NULL(n);
            MEL_ASSERT_EQ((isize)n->value, i * 7);
        }
    }

    Mel_SkipNode* curr = sl.header->forward[0];
    isize prev = -1;
    while (curr)
    {
        MEL_ASSERT_GT((isize)curr->key, prev);
        prev = (isize)curr->key;
        curr = curr->forward[0];
    }

    mel_skiplist_free(&sl);
}

MEL_TEST(duplicate_insert, .tags = "collection")
{
    Mel_SkipList sl;
    mel_skiplist_init(&sl, cmp_i32, mel_alloc_heap());

    bool first = mel_skiplist_insert(&sl, (void*)(isize)42, (void*)(isize)100);
    MEL_ASSERT(first);

    bool second = mel_skiplist_insert(&sl, (void*)(isize)42, (void*)(isize)200);
    MEL_ASSERT(!second);

    MEL_ASSERT_EQ(mel_skiplist_count(&sl), (usize)1);

    Mel_SkipNode* n = mel_skiplist_find(&sl, (void*)(isize)42);
    MEL_ASSERT_EQ((isize)n->value, (isize)100);

    mel_skiplist_free(&sl);
}

MEL_TEST(deterministic_seed, .tags = "collection")
{
    Mel_SkipList sl1;
    mel_skiplist_init(&sl1, cmp_i32, mel_alloc_heap());
    mel_skiplist_seed(&sl1, 0xDEADBEEFCAFEULL);

    Mel_SkipList sl2;
    mel_skiplist_init(&sl2, cmp_i32, mel_alloc_heap());
    mel_skiplist_seed(&sl2, 0xDEADBEEFCAFEULL);

    for (isize i = 0; i < 50; i++)
    {
        mel_skiplist_insert(&sl1, (void*)i, (void*)(i * 3));
        mel_skiplist_insert(&sl2, (void*)i, (void*)(i * 3));
    }

    MEL_ASSERT_EQ(sl1.level, sl2.level);
    MEL_ASSERT_EQ(mel_skiplist_count(&sl1), mel_skiplist_count(&sl2));

    Mel_SkipNode* n1 = sl1.header->forward[0];
    Mel_SkipNode* n2 = sl2.header->forward[0];
    while (n1 && n2)
    {
        MEL_ASSERT_EQ((isize)n1->key, (isize)n2->key);
        MEL_ASSERT_EQ(n1->level, n2->level);
        n1 = n1->forward[0];
        n2 = n2->forward[0];
    }
    MEL_ASSERT_NULL(n1);
    MEL_ASSERT_NULL(n2);

    mel_skiplist_free(&sl1);
    mel_skiplist_free(&sl2);
}
