#include "../src/test.h"
#include "../src/allocator.buddy.h"

MEL_TEST(buddy_init)
{
    _Alignas(64) u8 buffer[1024];
    u8 tree[256];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    MEL_ASSERT_EQ(buddy.size, sizeof(buffer));
    MEL_ASSERT_EQ(buddy.min_block, (usize)64);
    MEL_ASSERT_GT(buddy.levels, 0);
    MEL_PASS();
}

MEL_TEST(buddy_alloc_single)
{
    _Alignas(64) u8 buffer[1024];
    u8 tree[256];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    void* ptr = mel_buddy_alloc(&buddy, 32);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT((u8*)ptr >= buffer);
    MEL_ASSERT((u8*)ptr < buffer + sizeof(buffer));

    *(i32*)ptr = 42;
    MEL_ASSERT_EQ(*(i32*)ptr, 42);

    mel_buddy_free(&buddy, ptr);
    MEL_PASS();
}

MEL_TEST(buddy_alloc_multiple)
{
    _Alignas(64) u8 buffer[1024];
    u8 tree[256];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    void* a = mel_buddy_alloc(&buddy, 64);
    void* b = mel_buddy_alloc(&buddy, 64);
    void* c = mel_buddy_alloc(&buddy, 64);
    MEL_ASSERT_NOT_NULL(a);
    MEL_ASSERT_NOT_NULL(b);
    MEL_ASSERT_NOT_NULL(c);

    MEL_ASSERT(a != b);
    MEL_ASSERT(b != c);
    MEL_ASSERT(a != c);

    mel_buddy_free(&buddy, a);
    mel_buddy_free(&buddy, b);
    mel_buddy_free(&buddy, c);
    MEL_PASS();
}

MEL_TEST(buddy_power_of_two_rounding)
{
    _Alignas(64) u8 buffer[1024];
    u8 tree[256];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    void* ptr = mel_buddy_alloc(&buddy, 33);
    MEL_ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xAA, 33);
    MEL_ASSERT_EQ(((u8*)ptr)[0], 0xAA);
    MEL_ASSERT_EQ(((u8*)ptr)[32], 0xAA);

    mel_buddy_free(&buddy, ptr);
    MEL_PASS();
}

MEL_TEST(buddy_exhaust_min_blocks)
{
    _Alignas(64) u8 buffer[256];
    u8 tree[64];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    void* a = mel_buddy_alloc(&buddy, 64);
    void* b = mel_buddy_alloc(&buddy, 64);
    void* c = mel_buddy_alloc(&buddy, 64);
    void* d = mel_buddy_alloc(&buddy, 64);
    MEL_ASSERT_NOT_NULL(a);
    MEL_ASSERT_NOT_NULL(b);
    MEL_ASSERT_NOT_NULL(c);
    MEL_ASSERT_NOT_NULL(d);

    void* e = mel_buddy_alloc(&buddy, 64);
    MEL_ASSERT_NULL(e);

    mel_buddy_free(&buddy, a);
    mel_buddy_free(&buddy, b);
    mel_buddy_free(&buddy, c);
    mel_buddy_free(&buddy, d);
    MEL_PASS();
}

MEL_TEST(buddy_coalescing)
{
    _Alignas(64) u8 buffer[256];
    u8 tree[64];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    void* a = mel_buddy_alloc(&buddy, 64);
    void* b = mel_buddy_alloc(&buddy, 64);
    void* c = mel_buddy_alloc(&buddy, 64);
    void* d = mel_buddy_alloc(&buddy, 64);

    mel_buddy_free(&buddy, a);
    mel_buddy_free(&buddy, b);
    mel_buddy_free(&buddy, c);
    mel_buddy_free(&buddy, d);

    void* big = mel_buddy_alloc(&buddy, 256);
    MEL_ASSERT_NOT_NULL(big);

    mel_buddy_free(&buddy, big);
    MEL_PASS();
}

MEL_TEST(buddy_large_then_small)
{
    _Alignas(64) u8 buffer[1024];
    u8 tree[256];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    void* big = mel_buddy_alloc(&buddy, 512);
    MEL_ASSERT_NOT_NULL(big);

    void* small = mel_buddy_alloc(&buddy, 64);
    MEL_ASSERT_NOT_NULL(small);

    void* another = mel_buddy_alloc(&buddy, 256);
    MEL_ASSERT_NOT_NULL(another);

    void* toomuch = mel_buddy_alloc(&buddy, 256);
    MEL_ASSERT_NULL(toomuch);

    mel_buddy_free(&buddy, big);
    mel_buddy_free(&buddy, small);
    mel_buddy_free(&buddy, another);
    MEL_PASS();
}

MEL_TEST(buddy_too_large)
{
    _Alignas(64) u8 buffer[256];
    u8 tree[64];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    void* ptr = mel_buddy_alloc(&buddy, 512);
    MEL_ASSERT_NULL(ptr);
    MEL_PASS();
}

MEL_TEST(buddy_reset)
{
    _Alignas(64) u8 buffer[256];
    u8 tree[64];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    mel_buddy_alloc(&buddy, 64);
    mel_buddy_alloc(&buddy, 64);
    mel_buddy_alloc(&buddy, 64);
    mel_buddy_alloc(&buddy, 64);
    MEL_ASSERT_NULL(mel_buddy_alloc(&buddy, 64));

    mel_buddy_reset(&buddy);

    void* ptr = mel_buddy_alloc(&buddy, 256);
    MEL_ASSERT_NOT_NULL(ptr);
    mel_buddy_free(&buddy, ptr);
    MEL_PASS();
}

MEL_TEST(buddy_alloc_free_realloc_cycle)
{
    _Alignas(64) u8 buffer[1024];
    u8 tree[256];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    for (i32 i = 0; i < 50; i++)
    {
        void* ptr = mel_buddy_alloc(&buddy, 64);
        MEL_ASSERT_NOT_NULL(ptr);
        *(i32*)ptr = i;
        MEL_ASSERT_EQ(*(i32*)ptr, i);
        mel_buddy_free(&buddy, ptr);
    }
    MEL_PASS();
}

MEL_TEST(buddy_fragmentation_recovery)
{
    _Alignas(64) u8 buffer[256];
    u8 tree[64];
    Mel_Buddy_Alloc buddy;
    mel_buddy_init(&buddy, buffer, sizeof(buffer), .min_block_size = 64, .tree_buffer = tree);

    void* a = mel_buddy_alloc(&buddy, 64);
    void* b = mel_buddy_alloc(&buddy, 64);
    void* c = mel_buddy_alloc(&buddy, 64);
    void* d = mel_buddy_alloc(&buddy, 64);
    MEL_ASSERT_NOT_NULL(a);
    MEL_ASSERT_NOT_NULL(b);
    MEL_ASSERT_NOT_NULL(c);
    MEL_ASSERT_NOT_NULL(d);

    void* full = mel_buddy_alloc(&buddy, 64);
    MEL_ASSERT_NULL(full);

    mel_buddy_free(&buddy, a);
    mel_buddy_free(&buddy, b);

    void* medium = mel_buddy_alloc(&buddy, 128);
    MEL_ASSERT_NOT_NULL(medium);

    mel_buddy_free(&buddy, medium);
    mel_buddy_free(&buddy, c);
    mel_buddy_free(&buddy, d);

    void* big = mel_buddy_alloc(&buddy, 256);
    MEL_ASSERT_NOT_NULL(big);
    mel_buddy_free(&buddy, big);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Buddy Allocator Tests");

    MEL_RUN_TEST(buddy_init);
    MEL_RUN_TEST(buddy_alloc_single);
    MEL_RUN_TEST(buddy_alloc_multiple);
    MEL_RUN_TEST(buddy_power_of_two_rounding);
    MEL_RUN_TEST(buddy_exhaust_min_blocks);
    MEL_RUN_TEST(buddy_coalescing);
    MEL_RUN_TEST(buddy_large_then_small);
    MEL_RUN_TEST(buddy_too_large);
    MEL_RUN_TEST(buddy_reset);
    MEL_RUN_TEST(buddy_alloc_free_realloc_cycle);
    MEL_RUN_TEST(buddy_fragmentation_recovery);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
