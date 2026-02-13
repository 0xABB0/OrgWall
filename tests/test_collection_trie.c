#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.trie.h"
#include <string.h>

MEL_TEST(init_empty)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    MEL_ASSERT(mel_trie_empty(&t));
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)0);
    MEL_ASSERT_NOT_NULL(t.root);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(insert_single_find)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 val = 42;
    MEL_ASSERT(mel_trie_insert_str(&t, "hello", &val));
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)1);
    void* found = mel_trie_find_str(&t, "hello");
    MEL_ASSERT_NOT_NULL(found);
    MEL_ASSERT_EQ(*(i32*)found, 42);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(insert_multiple_find_all)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 a = 1, b = 2, c = 3;
    MEL_ASSERT(mel_trie_insert_str(&t, "foo", &a));
    MEL_ASSERT(mel_trie_insert_str(&t, "bar", &b));
    MEL_ASSERT(mel_trie_insert_str(&t, "baz", &c));
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)3);

    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "foo"), 1);
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "bar"), 2);
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "baz"), 3);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(find_nonexisting)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 val = 1;
    mel_trie_insert_str(&t, "hello", &val);
    MEL_ASSERT_NULL(mel_trie_find_str(&t, "world"));
    MEL_ASSERT_NULL(mel_trie_find_str(&t, "hell"));
    MEL_ASSERT_NULL(mel_trie_find_str(&t, "helloo"));
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(insert_duplicate)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 a = 1, b = 2;
    MEL_ASSERT(mel_trie_insert_str(&t, "key", &a));
    MEL_ASSERT(!mel_trie_insert_str(&t, "key", &b));
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)1);
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "key"), 1);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(remove_key)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 val = 99;
    mel_trie_insert_str(&t, "remove_me", &val);
    MEL_ASSERT(mel_trie_contains_str(&t, "remove_me"));
    MEL_ASSERT(mel_trie_remove_str(&t, "remove_me"));
    MEL_ASSERT(!mel_trie_contains_str(&t, "remove_me"));
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)0);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(remove_nonexisting)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    MEL_ASSERT(!mel_trie_remove_str(&t, "nope"));
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(remove_prefix_keeps_longer)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 a = 1, b = 2;
    mel_trie_insert_str(&t, "app", &a);
    mel_trie_insert_str(&t, "apple", &b);

    MEL_ASSERT(mel_trie_remove_str(&t, "app"));
    MEL_ASSERT(!mel_trie_contains_str(&t, "app"));
    MEL_ASSERT(mel_trie_contains_str(&t, "apple"));
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "apple"), 2);
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)1);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(remove_longer_keeps_prefix)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 a = 1, b = 2;
    mel_trie_insert_str(&t, "app", &a);
    mel_trie_insert_str(&t, "apple", &b);

    MEL_ASSERT(mel_trie_remove_str(&t, "apple"));
    MEL_ASSERT(!mel_trie_contains_str(&t, "apple"));
    MEL_ASSERT(mel_trie_contains_str(&t, "app"));
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "app"), 1);
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)1);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(starts_with_basic)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 a = 1, b = 2, c = 3;
    mel_trie_insert_str(&t, "app", &a);
    mel_trie_insert_str(&t, "apple", &b);
    mel_trie_insert_str(&t, "application", &c);

    MEL_ASSERT(mel_trie_starts_with_str(&t, "app"));
    MEL_ASSERT(mel_trie_starts_with_str(&t, "appl"));
    MEL_ASSERT(mel_trie_starts_with_str(&t, "ap"));
    MEL_ASSERT(mel_trie_starts_with_str(&t, "a"));
    MEL_ASSERT(!mel_trie_starts_with_str(&t, "b"));
    MEL_ASSERT(!mel_trie_starts_with_str(&t, "apps"));
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(contains_basic)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 val = 1;
    mel_trie_insert_str(&t, "test", &val);
    MEL_ASSERT(mel_trie_contains_str(&t, "test"));
    MEL_ASSERT(!mel_trie_contains_str(&t, "tes"));
    MEL_ASSERT(!mel_trie_contains_str(&t, "testing"));
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(count_tracking)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 vals[5] = {0, 1, 2, 3, 4};
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)0);
    mel_trie_insert_str(&t, "a", &vals[0]);
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)1);
    mel_trie_insert_str(&t, "b", &vals[1]);
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)2);
    mel_trie_insert_str(&t, "c", &vals[2]);
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)3);
    mel_trie_remove_str(&t, "b");
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)2);
    mel_trie_remove_str(&t, "a");
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)1);
    mel_trie_remove_str(&t, "c");
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)0);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(clear_resets)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 a = 1, b = 2;
    mel_trie_insert_str(&t, "alpha", &a);
    mel_trie_insert_str(&t, "beta", &b);
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)2);

    mel_trie_clear(&t);
    MEL_ASSERT(mel_trie_empty(&t));
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)0);
    MEL_ASSERT(!mel_trie_contains_str(&t, "alpha"));
    MEL_ASSERT(!mel_trie_contains_str(&t, "beta"));
    MEL_ASSERT_NOT_NULL(t.root);

    mel_trie_insert_str(&t, "gamma", &a);
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)1);
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(empty_string_key)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 val = 77;
    MEL_ASSERT(mel_trie_insert_str(&t, "", &val));
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)1);
    MEL_ASSERT(mel_trie_contains_str(&t, ""));
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, ""), 77);
    MEL_ASSERT(mel_trie_remove_str(&t, ""));
    MEL_ASSERT(!mel_trie_contains_str(&t, ""));
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(binary_keys)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    u8 key1[] = {0x00, 0x01, 0x02};
    u8 key2[] = {0xFF, 0xFE, 0xFD};
    u8 key3[] = {0x00, 0x00, 0x00};
    i32 a = 10, b = 20, c = 30;

    MEL_ASSERT(mel_trie_insert(&t, key1, 3, &a));
    MEL_ASSERT(mel_trie_insert(&t, key2, 3, &b));
    MEL_ASSERT(mel_trie_insert(&t, key3, 3, &c));
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)3);

    MEL_ASSERT_EQ(*(i32*)mel_trie_find(&t, key1, 3), 10);
    MEL_ASSERT_EQ(*(i32*)mel_trie_find(&t, key2, 3), 20);
    MEL_ASSERT_EQ(*(i32*)mel_trie_find(&t, key3, 3), 30);

    MEL_ASSERT(mel_trie_remove(&t, key2, 3));
    MEL_ASSERT(!mel_trie_contains(&t, key2, 3));
    MEL_ASSERT(mel_trie_contains(&t, key1, 3));
    MEL_ASSERT(mel_trie_contains(&t, key3, 3));
    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(stress_insert_remove)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());

    char keys[100][16];
    i32 vals[100];
    for (i32 i = 0; i < 100; i++)
    {
        snprintf(keys[i], sizeof(keys[i]), "key_%03d", i);
        vals[i] = i;
        MEL_ASSERT(mel_trie_insert_str(&t, keys[i], &vals[i]));
    }
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)100);

    for (i32 i = 0; i < 100; i++)
    {
        void* found = mel_trie_find_str(&t, keys[i]);
        MEL_ASSERT_NOT_NULL(found);
        MEL_ASSERT_EQ(*(i32*)found, i);
    }

    for (i32 i = 0; i < 50; i++)
    {
        MEL_ASSERT(mel_trie_remove_str(&t, keys[i * 2]));
    }
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)50);

    for (i32 i = 0; i < 50; i++)
    {
        MEL_ASSERT(!mel_trie_contains_str(&t, keys[i * 2]));
    }

    for (i32 i = 0; i < 50; i++)
    {
        i32 odd = i * 2 + 1;
        MEL_ASSERT(mel_trie_contains_str(&t, keys[odd]));
        MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, keys[odd]), odd);
    }

    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(common_prefixes)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    i32 a = 1, b = 2, c = 3, d = 4;

    mel_trie_insert_str(&t, "test", &a);
    mel_trie_insert_str(&t, "testing", &b);
    mel_trie_insert_str(&t, "tested", &c);
    mel_trie_insert_str(&t, "tester", &d);
    MEL_ASSERT_EQ(mel_trie_count(&t), (usize)4);

    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "test"), 1);
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "testing"), 2);
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "tested"), 3);
    MEL_ASSERT_EQ(*(i32*)mel_trie_find_str(&t, "tester"), 4);

    MEL_ASSERT(mel_trie_remove_str(&t, "test"));
    MEL_ASSERT(!mel_trie_contains_str(&t, "test"));
    MEL_ASSERT(mel_trie_contains_str(&t, "testing"));
    MEL_ASSERT(mel_trie_contains_str(&t, "tested"));
    MEL_ASSERT(mel_trie_contains_str(&t, "tester"));

    MEL_ASSERT(mel_trie_remove_str(&t, "tested"));
    MEL_ASSERT(mel_trie_contains_str(&t, "testing"));
    MEL_ASSERT(mel_trie_contains_str(&t, "tester"));

    MEL_ASSERT(mel_trie_starts_with_str(&t, "test"));
    MEL_ASSERT(!mel_trie_starts_with_str(&t, "testa"));

    mel_trie_free(&t);
    MEL_PASS();
}

MEL_TEST(find_returns_null_for_value)
{
    Mel_Trie t;
    mel_trie_init(&t, mel_alloc_heap());
    MEL_ASSERT(mel_trie_insert_str(&t, "null_val", nullptr));
    MEL_ASSERT(mel_trie_contains_str(&t, "null_val"));
    MEL_ASSERT_NULL(mel_trie_find_str(&t, "null_val"));
    mel_trie_free(&t);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Collection Trie Tests");

    MEL_RUN_TEST(init_empty);
    MEL_RUN_TEST(insert_single_find);
    MEL_RUN_TEST(insert_multiple_find_all);
    MEL_RUN_TEST(find_nonexisting);
    MEL_RUN_TEST(insert_duplicate);
    MEL_RUN_TEST(remove_key);
    MEL_RUN_TEST(remove_nonexisting);
    MEL_RUN_TEST(remove_prefix_keeps_longer);
    MEL_RUN_TEST(remove_longer_keeps_prefix);
    MEL_RUN_TEST(starts_with_basic);
    MEL_RUN_TEST(contains_basic);
    MEL_RUN_TEST(count_tracking);
    MEL_RUN_TEST(clear_resets);
    MEL_RUN_TEST(empty_string_key);
    MEL_RUN_TEST(binary_keys);
    MEL_RUN_TEST(stress_insert_remove);
    MEL_RUN_TEST(common_prefixes);
    MEL_RUN_TEST(find_returns_null_for_value);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
