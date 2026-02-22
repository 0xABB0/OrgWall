#include "../melody/test.harness.h"
#include "../melody/allocator.arena.h"

MEL_TEST(arena_init, .tags = "allocator")
{
    u8 buffer[512];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    MEL_ASSERT_EQ(arena.offset, (usize)0);
    MEL_ASSERT_EQ(arena.size, sizeof(buffer));
    MEL_ASSERT(arena.base == buffer);
}

MEL_TEST(arena_push_struct, .tags = "allocator")
{
    u8 buffer[1024];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    i32* a = mel_arena_push_struct(&arena, i32);
    MEL_ASSERT_NOT_NULL(a);
    *a = 100;

    i64* b = mel_arena_push_struct(&arena, i64);
    MEL_ASSERT_NOT_NULL(b);
    *b = 200;

    MEL_ASSERT_EQ(*a, 100);
    MEL_ASSERT_EQ(*b, (i64)200);
}

MEL_TEST(arena_push_array, .tags = "allocator")
{
    u8 buffer[4096];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    i32* arr = mel_arena_push_array(&arena, i32, 100);
    MEL_ASSERT_NOT_NULL(arr);
    for (i32 i = 0; i < 100; i++) arr[i] = i;
    for (i32 i = 0; i < 100; i++) MEL_ASSERT_EQ(arr[i], i);
}

MEL_TEST(arena_push_zero, .tags = "allocator")
{
    u8 buffer[256];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    memset(buffer, 0xFF, sizeof(buffer));
    mel_arena_init(&arena, buffer, sizeof(buffer));

    u8* ptr = (u8*)mel_arena_push_zero(&arena, 64);
    MEL_ASSERT_NOT_NULL(ptr);
    for (i32 i = 0; i < 64; i++) MEL_ASSERT_EQ(ptr[i], 0);
}

MEL_TEST(arena_alignment, .tags = "allocator")
{
    _Alignas(64) u8 buffer[1024];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    mel_arena_push(&arena, 1);
    void* aligned = mel_arena_push_align(&arena, 32, 16);
    MEL_ASSERT_EQ((usize)aligned % 16, (usize)0);

    mel_arena_push(&arena, 3);
    void* aligned2 = mel_arena_push_align(&arena, 32, 32);
    MEL_ASSERT_EQ((usize)aligned2 % 32, (usize)0);
}

MEL_TEST(arena_reset, .tags = "allocator")
{
    u8 buffer[512];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    mel_arena_push(&arena, 128);
    MEL_ASSERT_GT(arena.offset, (usize)0);

    mel_arena_reset(&arena);
    MEL_ASSERT_EQ(arena.offset, (usize)0);

    i32* ptr = mel_arena_push_struct(&arena, i32);
    MEL_ASSERT_NOT_NULL(ptr);
    *ptr = 42;
    MEL_ASSERT_EQ(*ptr, 42);
}

MEL_TEST(arena_fill_to_capacity, .tags = "allocator")
{
    u8 buffer[64];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    u8* all = (u8*)mel_arena_push(&arena, 64);
    MEL_ASSERT_NOT_NULL(all);
    MEL_ASSERT_EQ(arena.offset, (usize)64);
    for (i32 i = 0; i < 64; i++) all[i] = (u8)i;
    for (i32 i = 0; i < 64; i++) MEL_ASSERT_EQ(all[i], (u8)i);
}

MEL_TEST(arena_scratch_discard, .tags = "allocator")
{
    u8 buffer[1024];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    mel_arena_push(&arena, 64);
    usize before = arena.offset;

    Mel_Arena_Scratch scratch = mel_arena_scratch_begin(&arena);
    mel_arena_push(&arena, 128);
    MEL_ASSERT_GT(arena.offset, before);

    mel_arena_scratch_discard(scratch);
    MEL_ASSERT_EQ(arena.offset, before);
}

MEL_TEST(arena_scratch_keep, .tags = "allocator")
{
    u8 buffer[1024];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    Mel_Arena_Scratch scratch = mel_arena_scratch_begin(&arena);
    mel_arena_push(&arena, 128);
    usize after_push = arena.offset;

    mel_arena_scratch_keep(scratch);
    MEL_ASSERT_EQ(arena.offset, after_push);
}

MEL_TEST(arena_sequential_pushes_no_overlap, .tags = "allocator")
{
    u8 buffer[1024];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    u8* a = (u8*)mel_arena_push(&arena, 32);
    u8* b = (u8*)mel_arena_push(&arena, 32);
    MEL_ASSERT(b >= a + 32);

    memset(a, 0xAA, 32);
    memset(b, 0xBB, 32);
    for (i32 i = 0; i < 32; i++) MEL_ASSERT_EQ(a[i], 0xAA);
    for (i32 i = 0; i < 32; i++) MEL_ASSERT_EQ(b[i], 0xBB);
}

MEL_TEST(arena_copy, .tags = "allocator")
{
    u8 buffer[256];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    i32 src[] = {10, 20, 30, 40, 50};
    i32* copy = (i32*)mel__arena_push_copy(&arena, src, sizeof(src));
    MEL_ASSERT_NOT_NULL(copy);
    MEL_ASSERT(copy != src);
    for (i32 i = 0; i < 5; i++) MEL_ASSERT_EQ(copy[i], src[i]);
}