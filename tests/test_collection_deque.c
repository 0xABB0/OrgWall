#include "../melody/collection.deque.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/test.harness.h"

typedef Mel_Deque(i32) Deque_i32;

MEL_TEST(push_back_pop_front_fifo, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    mel_deque_push_back(&dq, 10);
    mel_deque_push_back(&dq, 20);
    mel_deque_push_back(&dq, 30);

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)3);
    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 10);
    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 20);
    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 30);
    MEL_ASSERT(mel_deque_empty(&dq));

    mel_deque_free(&dq);
}

MEL_TEST(push_front_pop_back_fifo, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    mel_deque_push_front(&dq, 10);
    mel_deque_push_front(&dq, 20);
    mel_deque_push_front(&dq, 30);

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)3);
    MEL_ASSERT_EQ(mel_deque_pop_back(&dq), 10);
    MEL_ASSERT_EQ(mel_deque_pop_back(&dq), 20);
    MEL_ASSERT_EQ(mel_deque_pop_back(&dq), 30);
    MEL_ASSERT(mel_deque_empty(&dq));

    mel_deque_free(&dq);
}

MEL_TEST(push_back_pop_back_lifo, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    mel_deque_push_back(&dq, 10);
    mel_deque_push_back(&dq, 20);
    mel_deque_push_back(&dq, 30);

    MEL_ASSERT_EQ(mel_deque_pop_back(&dq), 30);
    MEL_ASSERT_EQ(mel_deque_pop_back(&dq), 20);
    MEL_ASSERT_EQ(mel_deque_pop_back(&dq), 10);
    MEL_ASSERT(mel_deque_empty(&dq));

    mel_deque_free(&dq);
}

MEL_TEST(push_front_pop_front_lifo, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    mel_deque_push_front(&dq, 10);
    mel_deque_push_front(&dq, 20);
    mel_deque_push_front(&dq, 30);

    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 30);
    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 20);
    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 10);
    MEL_ASSERT(mel_deque_empty(&dq));

    mel_deque_free(&dq);
}

MEL_TEST(mixed_push_front_back_order, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    mel_deque_push_back(&dq, 3);
    mel_deque_push_back(&dq, 4);
    mel_deque_push_front(&dq, 2);
    mel_deque_push_front(&dq, 1);
    mel_deque_push_back(&dq, 5);

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)5);
    MEL_ASSERT_EQ(mel_deque_at(&dq, 0), 1);
    MEL_ASSERT_EQ(mel_deque_at(&dq, 1), 2);
    MEL_ASSERT_EQ(mel_deque_at(&dq, 2), 3);
    MEL_ASSERT_EQ(mel_deque_at(&dq, 3), 4);
    MEL_ASSERT_EQ(mel_deque_at(&dq, 4), 5);

    mel_deque_free(&dq);
}

MEL_TEST(growth_past_initial_capacity, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    for (i32 i = 0; i < 20; i++)
    {
        mel_deque_push_back(&dq, i);
    }

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)20);
    MEL_ASSERT_GE(dq.capacity, (usize)20);

    for (i32 i = 0; i < 20; i++)
    {
        MEL_ASSERT_EQ(mel_deque_at(&dq, i), i);
    }

    mel_deque_free(&dq);
}

MEL_TEST(peek_front_back, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    mel_deque_push_back(&dq, 100);
    MEL_ASSERT_EQ(mel_deque_peek_front(&dq), 100);
    MEL_ASSERT_EQ(mel_deque_peek_back(&dq), 100);

    mel_deque_push_back(&dq, 200);
    MEL_ASSERT_EQ(mel_deque_peek_front(&dq), 100);
    MEL_ASSERT_EQ(mel_deque_peek_back(&dq), 200);

    mel_deque_push_front(&dq, 50);
    MEL_ASSERT_EQ(mel_deque_peek_front(&dq), 50);
    MEL_ASSERT_EQ(mel_deque_peek_back(&dq), 200);

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)3);

    mel_deque_free(&dq);
}

MEL_TEST(clear, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    mel_deque_push_back(&dq, 1);
    mel_deque_push_back(&dq, 2);
    mel_deque_push_back(&dq, 3);
    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)3);

    mel_deque_clear(&dq);
    MEL_ASSERT(mel_deque_empty(&dq));
    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)0);

    mel_deque_push_back(&dq, 99);
    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)1);
    MEL_ASSERT_EQ(mel_deque_peek_front(&dq), 99);

    mel_deque_free(&dq);
}

MEL_TEST(wraparound_stress, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    for (i32 i = 0; i < 6; i++)
    {
        mel_deque_push_back(&dq, i);
    }

    for (i32 i = 0; i < 4; i++)
    {
        MEL_ASSERT_EQ(mel_deque_pop_front(&dq), i);
    }

    for (i32 i = 100; i < 110; i++)
    {
        mel_deque_push_back(&dq, i);
    }

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)12);
    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 4);
    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 5);
    for (i32 i = 100; i < 110; i++)
    {
        MEL_ASSERT_EQ(mel_deque_pop_front(&dq), i);
    }
    MEL_ASSERT(mel_deque_empty(&dq));

    mel_deque_free(&dq);
}

MEL_TEST(stress_alternating_1000, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    for (i32 i = 0; i < 1000; i++)
    {
        if (i % 2 == 0)
        {
            mel_deque_push_back(&dq, i);
        }
        else
        {
            mel_deque_push_front(&dq, i);
        }
    }

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)1000);

    i32 expected_front[500];
    i32 expected_back[500];
    i32 fi = 0;
    i32 bi = 0;

    for (i32 i = 0; i < 1000; i++)
    {
        if (i % 2 == 0)
        {
            expected_back[bi++] = i;
        }
        else
        {
            expected_front[fi++] = i;
        }
    }

    for (i32 i = fi - 1; i >= 0; i--)
    {
        MEL_ASSERT_EQ(mel_deque_pop_front(&dq), expected_front[i]);
    }
    for (i32 i = 0; i < bi; i++)
    {
        MEL_ASSERT_EQ(mel_deque_pop_front(&dq), expected_back[i]);
    }

    MEL_ASSERT(mel_deque_empty(&dq));

    mel_deque_free(&dq);
}

MEL_TEST(single_element, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    mel_deque_push_back(&dq, 42);
    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)1);
    MEL_ASSERT_EQ(mel_deque_peek_front(&dq), 42);
    MEL_ASSERT_EQ(mel_deque_peek_back(&dq), 42);
    MEL_ASSERT_EQ(mel_deque_at(&dq, 0), 42);
    MEL_ASSERT_EQ(mel_deque_pop_front(&dq), 42);
    MEL_ASSERT(mel_deque_empty(&dq));

    mel_deque_push_front(&dq, 99);
    MEL_ASSERT_EQ(mel_deque_pop_back(&dq), 99);
    MEL_ASSERT(mel_deque_empty(&dq));

    mel_deque_free(&dq);
}

MEL_TEST(empty_deque, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    MEL_ASSERT(mel_deque_empty(&dq));
    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)0);

    mel_deque_free(&dq);
}

MEL_TEST(growth_with_wraparound, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Deque_i32 dq;
    mel_deque_init(&dq, alloc);

    for (i32 i = 0; i < 8; i++)
    {
        mel_deque_push_back(&dq, i);
    }
    MEL_ASSERT_EQ(dq.capacity, (usize)8);

    for (i32 i = 0; i < 4; i++)
    {
        mel_deque_pop_front(&dq);
    }

    for (i32 i = 8; i < 12; i++)
    {
        mel_deque_push_back(&dq, i);
    }

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)8);
    MEL_ASSERT_EQ(dq.capacity, (usize)8);
    MEL_ASSERT_GT(dq.head, (usize)0);

    mel_deque_push_back(&dq, 12);

    MEL_ASSERT_EQ(mel_deque_count(&dq), (usize)9);
    MEL_ASSERT_EQ(dq.head, (usize)0);
    MEL_ASSERT_GE(dq.capacity, (usize)9);

    for (i32 i = 4; i <= 12; i++)
    {
        MEL_ASSERT_EQ(mel_deque_at(&dq, i - 4), i);
    }

    mel_deque_free(&dq);
}
