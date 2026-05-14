#include <allocator.guard/guard.h>
#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include "test.harness.h"

MEL_TEST(allocator_guard_aligned_alloc_roundtrip, .tags = "allocator")
{
    Mel_Guard_Allocator guard;
    mel_guard_init(&guard, (Mel_Guard_Allocator_Opt){
        .backing = mel_alloc_heap(),
        .pre_guard_size = 16,
        .post_guard_size = 16,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD |
                 MEL_GUARD_FLAG_CANARY_TAIL |
                 MEL_GUARD_FLAG_POISON_ALLOC |
                 MEL_GUARD_FLAG_POISON_FREE,
    });

    Mel_Alloc alloc = mel_guard_allocator(&guard);

    u8* p = mel_aligned_alloc(&alloc, 96, 64);
    MEL_ASSERT_NOT_NULL(p);
    MEL_ASSERT_EQ(((usize)p & 63u), 0);

    for (usize i = 0; i < 96; ++i)
        p[i] = (u8)(i ^ 0x5Au);

    p = mel_aligned_realloc(&alloc, p, 160, 64);
    MEL_ASSERT_NOT_NULL(p);
    MEL_ASSERT_EQ(((usize)p & 63u), 0);

    for (usize i = 0; i < 96; ++i)
        MEL_ASSERT_EQ(p[i], (u8)(i ^ 0x5Au));

    mel_aligned_dealloc(&alloc, p, 64);
    mel_guard_shutdown(&guard);
}

MEL_TEST(allocator_guard_protected_quarantine_tracks_state, .tags = "allocator")
{
    Mel_Guard_Allocator guard;
    mel_guard_init(&guard, (Mel_Guard_Allocator_Opt){
        .backing = mel_alloc_heap(),
        .pre_guard_size = 16,
        .quarantine_bytes = 1024,
        .page_protect_min_size = 1,
        .protected_overhead_budget = 1 << 20,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD |
                 MEL_GUARD_FLAG_PAGE_PROTECT |
                 MEL_GUARD_FLAG_QUARANTINE,
    });

    Mel_Alloc alloc = mel_guard_allocator(&guard);
    void* p = mel_alloc(&alloc, 128);
    MEL_ASSERT_NOT_NULL(p);

    Mel_Guard_Allocator_Stats before = mel_guard_stats(&guard);
    MEL_ASSERT_EQ(before.live_allocs, 1);
    MEL_ASSERT_EQ(before.protected_allocs, 1);

    mel_dealloc(&alloc, p);

    Mel_Guard_Allocator_Stats after = mel_guard_stats(&guard);
    MEL_ASSERT_EQ(after.live_allocs, 0);
    MEL_ASSERT_EQ(after.quarantined_allocs, 1);
    MEL_ASSERT_EQ(after.protected_allocs, 1);

    mel_guard_shutdown(&guard);
}
