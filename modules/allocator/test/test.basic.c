#include <allocator.guard/guard.h>
#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include "test.harness.h"

MEL_TEST(allocator_guard_alloc_free_roundtrip, .tags = "allocator")
{
    Mel_Guard_Allocator guard;
    mel_guard_init(&guard, (Mel_Guard_Allocator_Opt){
        .backing = mel_alloc_heap(),
        .pre_guard_size = 16,
        .post_guard_size = 16,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD | MEL_GUARD_FLAG_CANARY_TAIL |
                 MEL_GUARD_FLAG_POISON_ALLOC | MEL_GUARD_FLAG_POISON_FREE,
    });

    Mel_Alloc alloc = mel_guard_allocator(&guard);
    u8* p = mel_alloc(&alloc, 64);
    MEL_ASSERT_NOT_NULL(p);
    for (usize i = 0; i < 64; ++i) p[i] = (u8)i;
    mel_dealloc(&alloc, p);

    Mel_Guard_Allocator_Stats stats = mel_guard_stats(&guard);
    MEL_ASSERT_EQ(stats.live_allocs, 0);
    MEL_ASSERT_EQ(stats.live_bytes, 0);

    mel_guard_shutdown(&guard);
}

MEL_TEST(allocator_guard_realloc_preserves_prefix, .tags = "allocator")
{
    Mel_Guard_Allocator guard;
    mel_guard_init(&guard, (Mel_Guard_Allocator_Opt){
        .backing = mel_alloc_heap(),
        .pre_guard_size = 16,
        .post_guard_size = 16,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD | MEL_GUARD_FLAG_CANARY_TAIL,
    });

    Mel_Alloc alloc = mel_guard_allocator(&guard);
    u8* p = mel_alloc(&alloc, 8);
    MEL_ASSERT_NOT_NULL(p);
    for (u8 i = 0; i < 8; ++i) p[i] = (u8)(0x80u + i);

    p = mel_realloc(&alloc, p, 32);
    MEL_ASSERT_NOT_NULL(p);
    for (u8 i = 0; i < 8; ++i) MEL_ASSERT_EQ(p[i], (u8)(0x80u + i));

    mel_dealloc(&alloc, p);
    mel_guard_shutdown(&guard);
}
