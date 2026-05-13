#include "allocator.guard.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "test.harness.h"

MEL_TEST(allocator_guard_quarantine_tracks_freed_blocks, .tags = "allocator")
{
    Mel_Guard_Allocator guard;
    mel_guard_init(&guard, (Mel_Guard_Allocator_Opt){
        .backing = mel_alloc_heap(),
        .pre_guard_size = 8,
        .post_guard_size = 8,
        .quarantine_bytes = 1024,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD | MEL_GUARD_FLAG_CANARY_TAIL |
                 MEL_GUARD_FLAG_POISON_FREE | MEL_GUARD_FLAG_QUARANTINE,
    });

    Mel_Alloc alloc = mel_guard_allocator(&guard);
    void* p = mel_alloc(&alloc, 64);
    MEL_ASSERT_NOT_NULL(p);
    mel_dealloc(&alloc, p);

    Mel_Guard_Allocator_Stats stats = mel_guard_stats(&guard);
    MEL_ASSERT_EQ(stats.live_allocs, 0);
    MEL_ASSERT_EQ(stats.quarantined_allocs, 1);
    MEL_ASSERT_EQ(stats.quarantined_bytes, 64);

    mel_guard_shutdown(&guard);
}
