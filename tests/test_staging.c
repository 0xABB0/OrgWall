#include "../melody/test.harness.h"
#include "../melody/gpu.staging.h"
#include "../melody/gpu.buffer.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/allocator.arena.h"

#include <string.h>

static Mel_Staging make_test_staging(u8* backing, usize backing_size)
{
    Mel_Staging stg = {0};
    stg.alloc = mel_alloc_heap();
    mel_arena_init(&stg.arena, backing, backing_size);

    u32 initial_copies = 8;
    stg.copies = mel_alloc_array(stg.alloc, Mel_Staging_Copy, initial_copies);
    stg.copy_capacity = initial_copies;
    stg.copy_count = 0;
    return stg;
}

static void free_test_staging(Mel_Staging* stg)
{
    if (stg->copies)
        mel_dealloc(stg->alloc, stg->copies);
    *stg = (Mel_Staging){0};
}

MEL_TEST(staging_reset_clears_copy_count, .tags = "gpu")
{
    u8 backing[1024];
    Mel_Staging stg = make_test_staging(backing, sizeof(backing));

    stg.copy_count = 5;
    stg.arena.offset = 256;

    mel_staging_reset(&stg);

    MEL_ASSERT_EQ(stg.copy_count, (u32)0);
    MEL_ASSERT_EQ(stg.arena.offset, (usize)0);

    free_test_staging(&stg);
}

MEL_TEST(staging_arena_tracks_offset, .tags = "gpu")
{
    u8 backing[1024];
    Mel_Staging stg = make_test_staging(backing, sizeof(backing));

    MEL_ASSERT_EQ(stg.arena.offset, (usize)0);

    void* p1 = mel_arena_push(&stg.arena, 64);
    MEL_ASSERT_NOT_NULL(p1);
    MEL_ASSERT_GE(stg.arena.offset, (usize)64);

    void* p2 = mel_arena_push(&stg.arena, 128);
    MEL_ASSERT_NOT_NULL(p2);
    MEL_ASSERT_GE(stg.arena.offset, (usize)192);

    MEL_ASSERT(p2 != p1);

    free_test_staging(&stg);
}

MEL_TEST(staging_copy_list_grows, .tags = "gpu")
{
    u8 backing[4096];
    Mel_Staging stg = make_test_staging(backing, sizeof(backing));

    Mel_Gpu_Buffer* fake_dst = (Mel_Gpu_Buffer*)(uintptr_t)0xDEAD;

    for (u32 i = 0; i < 20; i++)
    {
        u32 data = i;

        void* staging_ptr = mel_arena_push(&stg.arena, sizeof(u32));
        memcpy(staging_ptr, &data, sizeof(u32));

        u64 src_offset = (u64)((u8*)staging_ptr - stg.arena.base);

        if (stg.copy_count >= stg.copy_capacity)
        {
            u32 new_cap = stg.copy_capacity * 2;
            stg.copies = mel_realloc(stg.alloc, stg.copies, sizeof(Mel_Staging_Copy) * new_cap);
            stg.copy_capacity = new_cap;
        }

        stg.copies[stg.copy_count++] = (Mel_Staging_Copy){
            .dst = fake_dst,
            .dst_offset = (u64)(i * sizeof(u32)),
            .src_offset = src_offset,
            .size = sizeof(u32),
        };
    }

    MEL_ASSERT_EQ(stg.copy_count, (u32)20);
    MEL_ASSERT_GE(stg.copy_capacity, (u32)20);

    for (u32 i = 0; i < 20; i++)
    {
        MEL_ASSERT_EQ(stg.copies[i].size, (u64)sizeof(u32));
        MEL_ASSERT_EQ(stg.copies[i].dst_offset, (u64)(i * sizeof(u32)));
    }

    free_test_staging(&stg);
}

MEL_TEST(staging_reset_allows_reuse, .tags = "gpu")
{
    u8 backing[256];
    Mel_Staging stg = make_test_staging(backing, sizeof(backing));

    mel_arena_push(&stg.arena, 200);
    stg.copy_count = 3;
    MEL_ASSERT_GE(stg.arena.offset, (usize)200);

    mel_staging_reset(&stg);

    MEL_ASSERT_EQ(stg.arena.offset, (usize)0);
    MEL_ASSERT_EQ(stg.copy_count, (u32)0);

    void* p = mel_arena_push(&stg.arena, 100);
    MEL_ASSERT_NOT_NULL(p);

    free_test_staging(&stg);
}

MEL_TEST(staging_copy_records_offsets_correctly, .tags = "gpu")
{
    u8 backing[1024];
    Mel_Staging stg = make_test_staging(backing, sizeof(backing));

    u32 data_a = 0xAABBCCDD;
    u32 data_b = 0x11223344;

    void* ptr_a = mel_arena_push(&stg.arena, sizeof(u32));
    memcpy(ptr_a, &data_a, sizeof(u32));
    u64 off_a = (u64)((u8*)ptr_a - stg.arena.base);

    void* ptr_b = mel_arena_push(&stg.arena, sizeof(u32));
    memcpy(ptr_b, &data_b, sizeof(u32));
    u64 off_b = (u64)((u8*)ptr_b - stg.arena.base);

    MEL_ASSERT(off_b > off_a);

    u32 read_a;
    memcpy(&read_a, stg.arena.base + off_a, sizeof(u32));
    MEL_ASSERT_EQ(read_a, (u32)0xAABBCCDD);

    u32 read_b;
    memcpy(&read_b, stg.arena.base + off_b, sizeof(u32));
    MEL_ASSERT_EQ(read_b, (u32)0x11223344);

    free_test_staging(&stg);
}

MEL_TEST(staging_empty_has_zero_copies, .tags = "gpu")
{
    u8 backing[256];
    Mel_Staging stg = make_test_staging(backing, sizeof(backing));

    MEL_ASSERT_EQ(stg.copy_count, (u32)0);
    MEL_ASSERT_EQ(stg.arena.offset, (usize)0);

    free_test_staging(&stg);
}
