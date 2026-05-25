#include <allocator/guard.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include "test.harness.h"

MEL_TEST(allocator_guard_page_protect_routes_selected_allocs, .tags = "allocator")
{
    Mel_Guard_Allocator guard;
    mel_guard_init(&guard, (Mel_Guard_Allocator_Opt){
        .backing = mel_alloc_heap(),
        .pre_guard_size = 16,
        .page_protect_min_size = 1,
        .protected_overhead_budget = 1 << 20,
        .sample_every = 1,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD | MEL_GUARD_FLAG_PAGE_PROTECT,
    });

    Mel_Alloc alloc = mel_guard_allocator(&guard);
    void* p = mel_alloc(&alloc, 32);
    MEL_ASSERT_NOT_NULL(p);

    Mel_Guard_Allocator_Stats stats = mel_guard_stats(&guard);
    MEL_ASSERT_EQ(stats.live_allocs, 1);
    MEL_ASSERT_EQ(stats.protected_allocs, 1);

    mel_dealloc(&alloc, p);
    mel_guard_shutdown(&guard);
}
