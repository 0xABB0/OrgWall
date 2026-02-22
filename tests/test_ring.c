#include "../melody/test.harness.h"
#include "../melody/allocator.ring.h"
#include <string.h>

MEL_TEST(ring_init, .tags = "allocator")
{
    u8 buffer[256];
    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, sizeof(buffer));

    MEL_ASSERT_EQ(ring.write_offset, (usize)0);
    MEL_ASSERT_EQ(ring.read_offset, (usize)0);
    MEL_ASSERT_EQ(ring.used, (usize)0);
    MEL_ASSERT_EQ(mel_ring_available(&ring), sizeof(buffer));
}

MEL_TEST(ring_push_pop_single, .tags = "allocator")
{
    u8 buffer[256];
    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, sizeof(buffer));

    i32* val = mel_ring_push_struct(&ring, i32);
    MEL_ASSERT_NOT_NULL(val);
    *val = 42;
    MEL_ASSERT_EQ(*val, 42);
    MEL_ASSERT_GT(ring.used, (usize)0);

    i32* peeked = (i32*)mel_ring_peek(&ring);
    MEL_ASSERT_EQ(*peeked, 42);

    mel_ring_pop(&ring);
    MEL_ASSERT_EQ(ring.used, (usize)0);
}

MEL_TEST(ring_fifo_order, .tags = "allocator")
{
    u8 buffer[512];
    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, sizeof(buffer));

    i32* a = mel_ring_push_struct(&ring, i32);
    *a = 10;
    i32* b = mel_ring_push_struct(&ring, i32);
    *b = 20;
    i32* c = mel_ring_push_struct(&ring, i32);
    *c = 30;

    i32* first = (i32*)mel_ring_peek(&ring);
    MEL_ASSERT_EQ(*first, 10);
    mel_ring_pop(&ring);

    i32* second = (i32*)mel_ring_peek(&ring);
    MEL_ASSERT_EQ(*second, 20);
    mel_ring_pop(&ring);

    i32* third = (i32*)mel_ring_peek(&ring);
    MEL_ASSERT_EQ(*third, 30);
    mel_ring_pop(&ring);

    MEL_ASSERT_EQ(ring.used, (usize)0);
}

MEL_TEST(ring_wraparound, .tags = "allocator")
{
    usize header_size = sizeof(Mel_Ring_Header);
    usize entry_size = header_size + sizeof(i32);

    usize buf_size = entry_size * 4;
    u8 buffer[256];
    MEL_ASSERT_LE(buf_size, sizeof(buffer));

    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, buf_size);

    i32* a = mel_ring_push_struct(&ring, i32);
    *a = 1;
    i32* b = mel_ring_push_struct(&ring, i32);
    *b = 2;
    i32* c = mel_ring_push_struct(&ring, i32);
    *c = 3;

    mel_ring_pop(&ring);
    mel_ring_pop(&ring);

    i32* d = mel_ring_push_struct(&ring, i32);
    MEL_ASSERT_NOT_NULL(d);
    *d = 4;

    i32* peeked = (i32*)mel_ring_peek(&ring);
    MEL_ASSERT_EQ(*peeked, 3);
    mel_ring_pop(&ring);

    peeked = (i32*)mel_ring_peek(&ring);
    MEL_ASSERT_EQ(*peeked, 4);
    mel_ring_pop(&ring);

    MEL_ASSERT_EQ(ring.used, (usize)0);
}

MEL_TEST(ring_full_returns_null, .tags = "allocator")
{
    usize header_size = sizeof(Mel_Ring_Header);
    usize entry_size = header_size + sizeof(i32);
    usize buf_size = entry_size * 2;
    u8 buffer[256];

    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, buf_size);

    i32* a = mel_ring_push_struct(&ring, i32);
    MEL_ASSERT_NOT_NULL(a);
    i32* b = mel_ring_push_struct(&ring, i32);
    MEL_ASSERT_NOT_NULL(b);

    i32* c = mel_ring_push_struct(&ring, i32);
    MEL_ASSERT_NULL(c);

    mel_ring_pop(&ring);
    mel_ring_pop(&ring);
}

MEL_TEST(ring_peek_empty, .tags = "allocator")
{
    u8 buffer[128];
    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, sizeof(buffer));

    void* ptr = mel_ring_peek(&ring);
    MEL_ASSERT_NULL(ptr);
}

MEL_TEST(ring_reset, .tags = "allocator")
{
    u8 buffer[256];
    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, sizeof(buffer));

    mel_ring_push_struct(&ring, i32);
    mel_ring_push_struct(&ring, i32);

    mel_ring_reset(&ring);
    MEL_ASSERT_EQ(ring.used, (usize)0);
    MEL_ASSERT_EQ(ring.write_offset, (usize)0);
    MEL_ASSERT_EQ(ring.read_offset, (usize)0);
    MEL_ASSERT_EQ(mel_ring_available(&ring), (usize)256);

    i32* fresh = mel_ring_push_struct(&ring, i32);
    MEL_ASSERT_NOT_NULL(fresh);
    *fresh = 99;
    MEL_ASSERT_EQ(*fresh, 99);
    mel_ring_pop(&ring);
}

MEL_TEST(ring_continuous_push_pop, .tags = "allocator")
{
    usize header_size = sizeof(Mel_Ring_Header);
    usize entry_size = header_size + sizeof(i32);
    usize buf_size = entry_size * 8;
    u8 buffer[512];
    MEL_ASSERT_LE(buf_size, sizeof(buffer));

    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, buf_size);

    for (i32 i = 0; i < 100; i++)
    {
        i32* val = mel_ring_push_struct(&ring, i32);
        MEL_ASSERT_NOT_NULL(val);
        *val = i;
        MEL_ASSERT_EQ(*val, i);

        mel_ring_pop(&ring);
    }

    MEL_ASSERT_EQ(ring.used, (usize)0);
}

MEL_TEST(ring_available_tracking, .tags = "allocator")
{
    u8 buffer[256];
    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, sizeof(buffer));

    usize initial = mel_ring_available(&ring);
    MEL_ASSERT_EQ(initial, (usize)256);

    mel_ring_push_struct(&ring, i32);
    usize after_one = mel_ring_available(&ring);
    MEL_ASSERT_LT(after_one, initial);

    mel_ring_pop(&ring);
    usize after_pop = mel_ring_available(&ring);
    MEL_ASSERT_EQ(after_pop, initial);
}

MEL_TEST(ring_data_survives_wrap, .tags = "allocator")
{
    usize header_size = sizeof(Mel_Ring_Header);
    usize entry_size = header_size + sizeof(i32);
    usize buf_size = entry_size * 3;
    u8 buffer[256];

    Mel_Ring_Alloc ring;
    mel_ring_init(&ring, buffer, buf_size);

    i32* a = mel_ring_push_struct(&ring, i32);
    *a = 100;
    i32* b = mel_ring_push_struct(&ring, i32);
    *b = 200;

    mel_ring_pop(&ring);
    mel_ring_pop(&ring);

    i32* c = mel_ring_push_struct(&ring, i32);
    *c = 300;
    i32* d = mel_ring_push_struct(&ring, i32);
    MEL_ASSERT_NOT_NULL(d);
    *d = 400;

    i32* p = (i32*)mel_ring_peek(&ring);
    MEL_ASSERT_EQ(*p, 300);
    mel_ring_pop(&ring);
    p = (i32*)mel_ring_peek(&ring);
    MEL_ASSERT_EQ(*p, 400);
    mel_ring_pop(&ring);
}