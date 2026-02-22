#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.heap.h"

typedef Mel_Heap(i32) I32Heap;

MEL_TEST(min_heap_push_pop_ascending, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    mel_heap_push(&h, 30, a < b);
    mel_heap_push(&h, 10, a < b);
    mel_heap_push(&h, 50, a < b);
    mel_heap_push(&h, 20, a < b);
    mel_heap_push(&h, 40, a < b);

    i32 prev = mel_heap_pop(&h, a < b);
    MEL_ASSERT_EQ(prev, 10);
    for (usize i = 0; i < 4; i++)
    {
        i32 val = mel_heap_pop(&h, a < b);
        MEL_ASSERT_GE(val, prev);
        prev = val;
    }
    MEL_ASSERT(mel_heap_empty(&h));

    mel_heap_free(&h);
}

MEL_TEST(max_heap_push_pop_descending, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    mel_heap_push(&h, 30, a > b);
    mel_heap_push(&h, 10, a > b);
    mel_heap_push(&h, 50, a > b);
    mel_heap_push(&h, 20, a > b);
    mel_heap_push(&h, 40, a > b);

    i32 prev = mel_heap_pop(&h, a > b);
    MEL_ASSERT_EQ(prev, 50);
    for (usize i = 0; i < 4; i++)
    {
        i32 val = mel_heap_pop(&h, a > b);
        MEL_ASSERT_LE(val, prev);
        prev = val;
    }
    MEL_ASSERT(mel_heap_empty(&h));

    mel_heap_free(&h);
}

MEL_TEST(peek_returns_root_without_removing, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    mel_heap_push(&h, 5, a < b);
    mel_heap_push(&h, 3, a < b);
    mel_heap_push(&h, 7, a < b);

    MEL_ASSERT_EQ(mel_heap_peek(&h), 3);
    MEL_ASSERT_EQ(mel_heap_count(&h), 3);

    mel_heap_free(&h);
}

MEL_TEST(push_sorted_ascending_pop_all, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    for (i32 i = 1; i <= 5; i++)
        mel_heap_push(&h, i, a < b);

    i32 prev = mel_heap_pop(&h, a < b);
    MEL_ASSERT_EQ(prev, 1);
    for (usize i = 0; i < 4; i++)
    {
        i32 val = mel_heap_pop(&h, a < b);
        MEL_ASSERT_GE(val, prev);
        prev = val;
    }

    mel_heap_free(&h);
}

MEL_TEST(push_sorted_descending_pop_all, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    for (i32 i = 5; i >= 1; i--)
        mel_heap_push(&h, i, a < b);

    i32 prev = mel_heap_pop(&h, a < b);
    MEL_ASSERT_EQ(prev, 1);
    for (usize i = 0; i < 4; i++)
    {
        i32 val = mel_heap_pop(&h, a < b);
        MEL_ASSERT_GE(val, prev);
        prev = val;
    }

    mel_heap_free(&h);
}

MEL_TEST(push_duplicates, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    mel_heap_push(&h, 7, a < b);
    mel_heap_push(&h, 7, a < b);
    mel_heap_push(&h, 3, a < b);
    mel_heap_push(&h, 3, a < b);
    mel_heap_push(&h, 5, a < b);

    MEL_ASSERT_EQ(mel_heap_count(&h), 5);

    i32 prev = mel_heap_pop(&h, a < b);
    MEL_ASSERT_EQ(prev, 3);
    for (usize i = 0; i < 4; i++)
    {
        i32 val = mel_heap_pop(&h, a < b);
        MEL_ASSERT_GE(val, prev);
        prev = val;
    }
    MEL_ASSERT(mel_heap_empty(&h));

    mel_heap_free(&h);
}

MEL_TEST(growth_100_values, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    for (i32 i = 0; i < 100; i++)
        mel_heap_push(&h, (i * 37) % 100, a < b);

    MEL_ASSERT_EQ(mel_heap_count(&h), 100);

    i32 prev = mel_heap_pop(&h, a < b);
    for (usize i = 0; i < 99; i++)
    {
        i32 val = mel_heap_pop(&h, a < b);
        MEL_ASSERT_GE(val, prev);
        prev = val;
    }

    mel_heap_free(&h);
}

MEL_TEST(clear, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    mel_heap_push(&h, 1, a < b);
    mel_heap_push(&h, 2, a < b);
    mel_heap_push(&h, 3, a < b);

    mel_heap_clear(&h);
    MEL_ASSERT_EQ(mel_heap_count(&h), 0);
    MEL_ASSERT(mel_heap_empty(&h));

    mel_heap_push(&h, 99, a < b);
    MEL_ASSERT_EQ(mel_heap_peek(&h), 99);

    mel_heap_free(&h);
}

MEL_TEST(count_and_empty, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    MEL_ASSERT(mel_heap_empty(&h));
    MEL_ASSERT_EQ(mel_heap_count(&h), 0);

    mel_heap_push(&h, 42, a < b);
    MEL_ASSERT(!mel_heap_empty(&h));
    MEL_ASSERT_EQ(mel_heap_count(&h), 1);

    mel_heap_push(&h, 13, a < b);
    MEL_ASSERT_EQ(mel_heap_count(&h), 2);

    mel_heap_pop(&h, a < b);
    MEL_ASSERT_EQ(mel_heap_count(&h), 1);

    mel_heap_pop(&h, a < b);
    MEL_ASSERT(mel_heap_empty(&h));

    mel_heap_free(&h);
}

MEL_TEST(single_element, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    mel_heap_push(&h, 777, a < b);
    MEL_ASSERT_EQ(mel_heap_count(&h), 1);
    MEL_ASSERT_EQ(mel_heap_peek(&h), 777);

    i32 val = mel_heap_pop(&h, a < b);
    MEL_ASSERT_EQ(val, 777);
    MEL_ASSERT(mel_heap_empty(&h));

    mel_heap_free(&h);
}

MEL_TEST(stress_1000_values, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    for (u32 i = 0; i < 1000; i++)
        mel_heap_push(&h, (i32)((i * 2654435761u) % 1000), a < b);

    MEL_ASSERT_EQ(mel_heap_count(&h), 1000);

    i32 prev = mel_heap_pop(&h, a < b);
    for (usize i = 0; i < 999; i++)
    {
        i32 val = mel_heap_pop(&h, a < b);
        MEL_ASSERT_GE(val, prev);
        prev = val;
    }
    MEL_ASSERT(mel_heap_empty(&h));

    mel_heap_free(&h);
}

MEL_TEST(mixed_push_pop, .tags = "collection")
{
    I32Heap h;
    mel_heap_init(&h, mel_alloc_heap());

    mel_heap_push(&h, 50, a < b);
    mel_heap_push(&h, 30, a < b);
    mel_heap_push(&h, 70, a < b);
    mel_heap_push(&h, 10, a < b);
    mel_heap_push(&h, 90, a < b);

    i32 v1 = mel_heap_pop(&h, a < b);
    i32 v2 = mel_heap_pop(&h, a < b);
    MEL_ASSERT_EQ(v1, 10);
    MEL_ASSERT_EQ(v2, 30);
    MEL_ASSERT_EQ(mel_heap_count(&h), 3);

    mel_heap_push(&h, 20, a < b);
    mel_heap_push(&h, 60, a < b);
    mel_heap_push(&h, 5, a < b);

    MEL_ASSERT_EQ(mel_heap_count(&h), 6);

    i32 prev = mel_heap_pop(&h, a < b);
    MEL_ASSERT_EQ(prev, 5);
    for (usize i = 0; i < 5; i++)
    {
        i32 val = mel_heap_pop(&h, a < b);
        MEL_ASSERT_GE(val, prev);
        prev = val;
    }
    MEL_ASSERT(mel_heap_empty(&h));

    mel_heap_free(&h);
}
