#include <test/test.h>

#include <collection/slotmap.h>
#include <allocator/heap.h>

// Two-phase removal for future-gated reclamation (gpu-rhi.md §3.3). remove_deferred must roll the
// generation now (use-after-free stays loud) yet withhold the index from reuse until reclaim.

MEL_TEST(slotmap, remove_deferred_holds_index_until_reclaim)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(u32), .initial_capacity = 8);

    u32                a = 100, b = 101, c = 102, d = 103;
    Mel_SlotMap_Handle hA = mel_slotmap_insert(&sm, &a);
    Mel_SlotMap_Handle hB = mel_slotmap_insert(&sm, &b);
    (void)hB;

    // Deferred remove: the handle is immediately dead (use-after-free is caught), payload gone.
    MEL_REQUIRE(mel_slotmap_remove_deferred(&sm, hA));
    MEL_EXPECT(!mel_slotmap_alive(&sm, hA));
    MEL_EXPECT_NULL(mel_slotmap_get(&sm, hA));

    // A new insert must NOT reuse A's withheld index.
    Mel_SlotMap_Handle hC = mel_slotmap_insert(&sm, &c);
    MEL_EXPECT_NEQ(hC.index, hA.index);

    // After reclaim, the index returns to the free pool and the next insert reuses it.
    MEL_REQUIRE(mel_slotmap_reclaim(&sm, hA.index));
    Mel_SlotMap_Handle hD = mel_slotmap_insert(&sm, &d);
    MEL_EXPECT_EQ(hD.index, hA.index);
    MEL_EXPECT_NEQ(hD.generation, hA.generation); // a distinct handle at the reused slot
    MEL_REQUIRE_NOT_NULL(mel_slotmap_get(&sm, hD));
    MEL_EXPECT_EQ(*(u32*)mel_slotmap_get(&sm, hD), 103u);

    mel_slotmap_free(&sm);
}

MEL_TEST(slotmap, reclaim_rejects_live_and_double)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(u32), .initial_capacity = 8);

    u32                a = 7;
    Mel_SlotMap_Handle hA = mel_slotmap_insert(&sm, &a);

    // A live slot cannot be reclaimed.
    MEL_EXPECT(!mel_slotmap_reclaim(&sm, hA.index));

    MEL_REQUIRE(mel_slotmap_remove_deferred(&sm, hA));
    MEL_REQUIRE(mel_slotmap_reclaim(&sm, hA.index)); // first reclaim succeeds
    MEL_EXPECT(!mel_slotmap_reclaim(&sm, hA.index));  // second is a no-op (no longer held)

    mel_slotmap_free(&sm);
}

MEL_TEST(slotmap, deferred_does_not_disturb_other_slots)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(u32), .initial_capacity = 4);

    Mel_SlotMap_Handle h[6];
    u32                v[6] = { 10, 11, 12, 13, 14, 15 };
    for (u32 i = 0; i < 6; i++)
        h[i] = mel_slotmap_insert(&sm, &v[i]);

    // Deferred-remove a couple of interior slots; the dense swap-remove must keep every survivor readable.
    MEL_REQUIRE(mel_slotmap_remove_deferred(&sm, h[1]));
    MEL_REQUIRE(mel_slotmap_remove_deferred(&sm, h[3]));
    MEL_EXPECT_EQ(mel_slotmap_count(&sm), 4u);

    for (u32 i = 0; i < 6; i++)
    {
        if (i == 1 || i == 3)
        {
            MEL_EXPECT_NULL(mel_slotmap_get(&sm, h[i]));
            continue;
        }
        u32* p = mel_slotmap_get(&sm, h[i]);
        MEL_REQUIRE_NOT_NULL(p);
        MEL_EXPECT_EQ(*p, v[i]);
    }

    mel_slotmap_reclaim(&sm, h[1].index);
    mel_slotmap_reclaim(&sm, h[3].index);
    mel_slotmap_free(&sm);
}
