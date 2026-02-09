#include "../melody/test.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.queue.h"

typedef Mel_Queue(i32) I32Queue;

MEL_TEST(queue_basic_fifo)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    mel_queue_push(&q, 10);
    mel_queue_push(&q, 20);
    mel_queue_push(&q, 30);

    MEL_ASSERT_EQ(mel_queue_pop(&q), 10);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 20);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 30);
    MEL_ASSERT(mel_queue_empty(&q));

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_push_many_pop_all)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    for (i32 i = 0; i < 50; i++)
        mel_queue_push(&q, i * 3);

    for (i32 i = 0; i < 50; i++)
        MEL_ASSERT_EQ(mel_queue_pop(&q), i * 3);

    MEL_ASSERT(mel_queue_empty(&q));

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_interleaved)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    mel_queue_push(&q, 1);
    mel_queue_push(&q, 2);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 1);

    mel_queue_push(&q, 3);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 2);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 3);

    mel_queue_push(&q, 4);
    mel_queue_push(&q, 5);
    mel_queue_push(&q, 6);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 4);
    mel_queue_push(&q, 7);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 5);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 6);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 7);

    MEL_ASSERT(mel_queue_empty(&q));

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_growth)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    for (i32 i = 0; i < 100; i++)
        mel_queue_push(&q, i);

    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)100);
    MEL_ASSERT_GE(q.capacity, (usize)100);

    for (i32 i = 0; i < 100; i++)
        MEL_ASSERT_EQ(mel_queue_pop(&q), i);

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_peek)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    mel_queue_push(&q, 42);
    mel_queue_push(&q, 99);
    mel_queue_push(&q, 7);

    MEL_ASSERT_EQ(mel_queue_peek(&q), 42);
    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)3);

    MEL_ASSERT_EQ(mel_queue_peek_back(&q), 7);
    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)3);

    mel_queue_pop(&q);
    MEL_ASSERT_EQ(mel_queue_peek(&q), 99);
    MEL_ASSERT_EQ(mel_queue_peek_back(&q), 7);

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_clear)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    for (i32 i = 0; i < 20; i++)
        mel_queue_push(&q, i);

    usize cap_before = q.capacity;
    mel_queue_clear(&q);

    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)0);
    MEL_ASSERT(mel_queue_empty(&q));
    MEL_ASSERT_EQ(q.capacity, cap_before);

    mel_queue_push(&q, 999);
    MEL_ASSERT_EQ(mel_queue_peek(&q), 999);
    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)1);

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_at)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    for (i32 i = 0; i < 10; i++)
        mel_queue_push(&q, i * 10);

    for (usize i = 0; i < 10; i++)
        MEL_ASSERT_EQ(mel_queue_at(&q, i), (i32)(i * 10));

    mel_queue_pop(&q);
    mel_queue_pop(&q);

    MEL_ASSERT_EQ(mel_queue_at(&q, 0), 20);
    MEL_ASSERT_EQ(mel_queue_at(&q, 7), 90);

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_single_element)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    mel_queue_push(&q, 77);
    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)1);
    MEL_ASSERT(!mel_queue_empty(&q));
    MEL_ASSERT_EQ(mel_queue_peek(&q), 77);
    MEL_ASSERT_EQ(mel_queue_peek_back(&q), 77);
    MEL_ASSERT_EQ(mel_queue_at(&q, 0), 77);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 77);
    MEL_ASSERT(mel_queue_empty(&q));

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_stress)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    for (i32 i = 0; i < 1000; i++)
        mel_queue_push(&q, i);

    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)1000);

    for (i32 i = 0; i < 1000; i++)
        MEL_ASSERT_EQ(mel_queue_pop(&q), i);

    MEL_ASSERT(mel_queue_empty(&q));

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_wrap_around)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    mel_queue_push(&q, 10);
    mel_queue_push(&q, 20);
    mel_queue_push(&q, 30);
    mel_queue_push(&q, 40);
    mel_queue_push(&q, 50);

    MEL_ASSERT_EQ(mel_queue_pop(&q), 10);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 20);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 30);

    mel_queue_push(&q, 60);
    mel_queue_push(&q, 70);
    mel_queue_push(&q, 80);
    mel_queue_push(&q, 90);
    mel_queue_push(&q, 100);

    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)7);

    MEL_ASSERT_EQ(mel_queue_pop(&q), 40);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 50);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 60);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 70);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 80);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 90);
    MEL_ASSERT_EQ(mel_queue_pop(&q), 100);

    MEL_ASSERT(mel_queue_empty(&q));

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_growth_with_wrap)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    for (i32 i = 0; i < 6; i++)
        mel_queue_push(&q, i);

    for (i32 i = 0; i < 4; i++)
        mel_queue_pop(&q);

    for (i32 i = 6; i < 16; i++)
        mel_queue_push(&q, i);

    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)12);

    for (i32 i = 4; i < 16; i++)
        MEL_ASSERT_EQ(mel_queue_pop(&q), i);

    MEL_ASSERT(mel_queue_empty(&q));

    mel_queue_free(&q);
    MEL_PASS();
}

MEL_TEST(queue_at_with_wrap)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    I32Queue q;
    mel_queue_init(&q, alloc);

    for (i32 i = 0; i < 8; i++)
        mel_queue_push(&q, i);

    for (i32 i = 0; i < 5; i++)
        mel_queue_pop(&q);

    for (i32 i = 8; i < 11; i++)
        mel_queue_push(&q, i);

    MEL_ASSERT_EQ(mel_queue_count(&q), (usize)6);
    MEL_ASSERT_EQ(mel_queue_at(&q, 0), 5);
    MEL_ASSERT_EQ(mel_queue_at(&q, 1), 6);
    MEL_ASSERT_EQ(mel_queue_at(&q, 2), 7);
    MEL_ASSERT_EQ(mel_queue_at(&q, 3), 8);
    MEL_ASSERT_EQ(mel_queue_at(&q, 4), 9);
    MEL_ASSERT_EQ(mel_queue_at(&q, 5), 10);

    mel_queue_free(&q);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Collection Queue Tests");

    MEL_RUN_TEST(queue_basic_fifo);
    MEL_RUN_TEST(queue_push_many_pop_all);
    MEL_RUN_TEST(queue_interleaved);
    MEL_RUN_TEST(queue_growth);
    MEL_RUN_TEST(queue_peek);
    MEL_RUN_TEST(queue_clear);
    MEL_RUN_TEST(queue_at);
    MEL_RUN_TEST(queue_single_element);
    MEL_RUN_TEST(queue_stress);
    MEL_RUN_TEST(queue_wrap_around);
    MEL_RUN_TEST(queue_growth_with_wrap);
    MEL_RUN_TEST(queue_at_with_wrap);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
