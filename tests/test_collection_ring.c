#include "../melody/test.harness.h"
#include "../melody/collection.ring.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"

typedef Mel_Ring(i32) Ring_i32;

MEL_TEST(basic_push_pop)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 8, alloc);

    mel_ring_push(&ring, 10);
    mel_ring_push(&ring, 20);
    mel_ring_push(&ring, 30);

    MEL_ASSERT_EQ(mel_ring_pop(&ring), 10);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 20);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 30);
    MEL_ASSERT(mel_ring_empty(&ring));

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(fill_to_capacity)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 4, alloc);

    mel_ring_push(&ring, 1);
    mel_ring_push(&ring, 2);
    mel_ring_push(&ring, 3);
    mel_ring_push(&ring, 4);

    MEL_ASSERT(mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)4);
    MEL_ASSERT_EQ(mel_ring_capacity(&ring), (usize)4);

    MEL_ASSERT_EQ(mel_ring_pop(&ring), 1);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 2);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 3);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 4);

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(overwrite_oldest)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 3, alloc);

    mel_ring_push(&ring, 1);
    mel_ring_push(&ring, 2);
    mel_ring_push(&ring, 3);
    MEL_ASSERT(mel_ring_full(&ring));

    mel_ring_push(&ring, 4);
    MEL_ASSERT(mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)3);

    MEL_ASSERT_EQ(mel_ring_pop(&ring), 2);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 3);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 4);
    MEL_ASSERT(mel_ring_empty(&ring));

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(pop_after_overwrite)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 4, alloc);

    for (i32 i = 0; i < 10; i++) {
        mel_ring_push(&ring, i);
    }

    MEL_ASSERT(mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)4);

    MEL_ASSERT_EQ(mel_ring_pop(&ring), 6);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 7);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 8);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 9);
    MEL_ASSERT(mel_ring_empty(&ring));

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(peek_front_back)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 5, alloc);

    mel_ring_push(&ring, 10);
    mel_ring_push(&ring, 20);
    mel_ring_push(&ring, 30);

    MEL_ASSERT_EQ(mel_ring_peek(&ring), 10);
    MEL_ASSERT_EQ(mel_ring_peek_back(&ring), 30);

    mel_ring_push(&ring, 40);
    mel_ring_push(&ring, 50);
    mel_ring_push(&ring, 60);

    MEL_ASSERT_EQ(mel_ring_peek(&ring), 20);
    MEL_ASSERT_EQ(mel_ring_peek_back(&ring), 60);

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(at_indexing)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 5, alloc);

    mel_ring_push(&ring, 100);
    mel_ring_push(&ring, 200);
    mel_ring_push(&ring, 300);
    mel_ring_push(&ring, 400);

    MEL_ASSERT_EQ(mel_ring_at(&ring, 0), 100);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 1), 200);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 2), 300);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 3), 400);

    mel_ring_push(&ring, 500);
    mel_ring_push(&ring, 600);

    MEL_ASSERT_EQ(mel_ring_at(&ring, 0), 200);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 1), 300);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 2), 400);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 3), 500);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 4), 600);

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(clear)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 5, alloc);

    mel_ring_push(&ring, 1);
    mel_ring_push(&ring, 2);
    mel_ring_push(&ring, 3);

    mel_ring_clear(&ring);
    MEL_ASSERT(mel_ring_empty(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)0);
    MEL_ASSERT(!mel_ring_full(&ring));

    mel_ring_push(&ring, 10);
    MEL_ASSERT_EQ(mel_ring_peek(&ring), 10);
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)1);

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(count_full_empty)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 3, alloc);

    MEL_ASSERT(mel_ring_empty(&ring));
    MEL_ASSERT(!mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)0);
    MEL_ASSERT_EQ(mel_ring_capacity(&ring), (usize)3);

    mel_ring_push(&ring, 1);
    MEL_ASSERT(!mel_ring_empty(&ring));
    MEL_ASSERT(!mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)1);

    mel_ring_push(&ring, 2);
    mel_ring_push(&ring, 3);
    MEL_ASSERT(!mel_ring_empty(&ring));
    MEL_ASSERT(mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)3);

    mel_ring_pop(&ring);
    MEL_ASSERT(!mel_ring_empty(&ring));
    MEL_ASSERT(!mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)2);

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(push_capacity_then_pop_all)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 6, alloc);

    for (i32 i = 0; i < 6; i++) {
        mel_ring_push(&ring, i * 10);
    }

    MEL_ASSERT(mel_ring_full(&ring));

    for (i32 i = 0; i < 6; i++) {
        MEL_ASSERT_EQ(mel_ring_pop(&ring), i * 10);
    }

    MEL_ASSERT(mel_ring_empty(&ring));

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(continuous_overwrite_100_into_10)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 10, alloc);

    for (i32 i = 0; i < 100; i++) {
        mel_ring_push(&ring, i);
    }

    MEL_ASSERT(mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)10);

    for (i32 i = 0; i < 10; i++) {
        MEL_ASSERT_EQ(mel_ring_at(&ring, i), 90 + i);
    }

    MEL_ASSERT_EQ(mel_ring_peek(&ring), 90);
    MEL_ASSERT_EQ(mel_ring_peek_back(&ring), 99);

    for (i32 i = 0; i < 10; i++) {
        MEL_ASSERT_EQ(mel_ring_pop(&ring), 90 + i);
    }

    MEL_ASSERT(mel_ring_empty(&ring));

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(capacity_one)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 1, alloc);

    MEL_ASSERT(mel_ring_empty(&ring));
    MEL_ASSERT_EQ(mel_ring_capacity(&ring), (usize)1);

    mel_ring_push(&ring, 42);
    MEL_ASSERT(mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_peek(&ring), 42);
    MEL_ASSERT_EQ(mel_ring_peek_back(&ring), 42);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 0), 42);

    mel_ring_push(&ring, 99);
    MEL_ASSERT(mel_ring_full(&ring));
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)1);
    MEL_ASSERT_EQ(mel_ring_peek(&ring), 99);

    MEL_ASSERT_EQ(mel_ring_pop(&ring), 99);
    MEL_ASSERT(mel_ring_empty(&ring));

    for (i32 i = 0; i < 50; i++) {
        mel_ring_push(&ring, i);
    }
    MEL_ASSERT_EQ(mel_ring_peek(&ring), 49);

    mel_ring_free(&ring);
    MEL_PASS();
}

MEL_TEST(interleaved_push_pop_with_overwrite)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Ring_i32 ring;
    mel_ring_init(&ring, 3, alloc);

    mel_ring_push(&ring, 1);
    mel_ring_push(&ring, 2);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 1);

    mel_ring_push(&ring, 3);
    mel_ring_push(&ring, 4);
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)3);
    MEL_ASSERT(mel_ring_full(&ring));

    mel_ring_push(&ring, 5);
    MEL_ASSERT_EQ(mel_ring_count(&ring), (usize)3);
    MEL_ASSERT_EQ(mel_ring_peek(&ring), 3);

    MEL_ASSERT_EQ(mel_ring_pop(&ring), 3);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 4);
    MEL_ASSERT_EQ(mel_ring_pop(&ring), 5);
    MEL_ASSERT(mel_ring_empty(&ring));

    mel_ring_push(&ring, 100);
    mel_ring_push(&ring, 200);
    mel_ring_push(&ring, 300);
    mel_ring_push(&ring, 400);
    mel_ring_push(&ring, 500);

    MEL_ASSERT_EQ(mel_ring_at(&ring, 0), 300);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 1), 400);
    MEL_ASSERT_EQ(mel_ring_at(&ring, 2), 500);

    MEL_ASSERT_EQ(mel_ring_pop(&ring), 300);

    mel_ring_push(&ring, 600);
    MEL_ASSERT_EQ(mel_ring_peek(&ring), 400);
    MEL_ASSERT_EQ(mel_ring_peek_back(&ring), 600);

    mel_ring_free(&ring);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Collection Ring Buffer Tests");

    MEL_RUN_TEST(basic_push_pop);
    MEL_RUN_TEST(fill_to_capacity);
    MEL_RUN_TEST(overwrite_oldest);
    MEL_RUN_TEST(pop_after_overwrite);
    MEL_RUN_TEST(peek_front_back);
    MEL_RUN_TEST(at_indexing);
    MEL_RUN_TEST(clear);
    MEL_RUN_TEST(count_full_empty);
    MEL_RUN_TEST(push_capacity_then_pop_all);
    MEL_RUN_TEST(continuous_overwrite_100_into_10);
    MEL_RUN_TEST(capacity_one);
    MEL_RUN_TEST(interleaved_push_pop_with_overwrite);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
