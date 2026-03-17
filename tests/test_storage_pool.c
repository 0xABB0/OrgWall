#include "../melody/test.harness.h"
#include "../melody/collection.slotmap.h"
#include "../melody/collection.bitset.h"
#include "../melody/allocator.heap.h"

typedef struct {
    f32 x, y, z, w;
} Test_Storage_Item;

MEL_TEST(storage_pool_slotmap_alloc_and_get, .tags = "gpu, render")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(Test_Storage_Item), .initial_capacity = 8);

    Test_Storage_Item item = { .x = 1.0f, .y = 2.0f, .z = 3.0f, .w = 4.0f };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&sm, &item);
    MEL_ASSERT(mel_slotmap_alive(&sm, h));

    Test_Storage_Item* got = mel_slotmap_get(&sm, h);
    MEL_ASSERT_NOT_NULL(got);
    MEL_ASSERT_FLOAT_EQ(got->x, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(got->y, 2.0f, 0.001f);

    mel_slotmap_free(&sm);
}

MEL_TEST(storage_pool_dirty_tracking, .tags = "gpu, render")
{
    Mel_BitSet dirty;
    mel_bitset_init(&dirty, 32, mel_alloc_heap());

    MEL_ASSERT(mel_bitset_none(&dirty));

    mel_bitset_set(&dirty, 0);
    mel_bitset_set(&dirty, 5);
    mel_bitset_set(&dirty, 31);

    MEL_ASSERT(mel_bitset_any(&dirty));
    MEL_ASSERT(mel_bitset_get(&dirty, 0));
    MEL_ASSERT(mel_bitset_get(&dirty, 5));
    MEL_ASSERT(mel_bitset_get(&dirty, 31));
    MEL_ASSERT(!mel_bitset_get(&dirty, 1));

    MEL_ASSERT_EQ(mel_bitset_count_set(&dirty), (usize)3);

    mel_bitset_clear(&dirty);
    MEL_ASSERT(mel_bitset_none(&dirty));

    mel_bitset_free(&dirty);
}

MEL_TEST(storage_pool_handle_reuse_after_free, .tags = "gpu, render")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(Test_Storage_Item), .initial_capacity = 4);

    Test_Storage_Item a = { .x = 1.0f };
    Test_Storage_Item b = { .x = 2.0f };

    Mel_SlotMap_Handle ha = mel_slotmap_insert(&sm, &a);
    Mel_SlotMap_Handle hb = mel_slotmap_insert(&sm, &b);
    MEL_ASSERT_EQ(mel_slotmap_count(&sm), (u32)2);

    mel_slotmap_remove(&sm, ha);
    MEL_ASSERT_EQ(mel_slotmap_count(&sm), (u32)1);
    MEL_ASSERT(!mel_slotmap_alive(&sm, ha));
    MEL_ASSERT(mel_slotmap_alive(&sm, hb));

    Test_Storage_Item c = { .x = 3.0f };
    Mel_SlotMap_Handle hc = mel_slotmap_insert(&sm, &c);
    MEL_ASSERT_EQ(mel_slotmap_count(&sm), (u32)2);
    MEL_ASSERT(mel_slotmap_alive(&sm, hc));

    Test_Storage_Item* got = mel_slotmap_get(&sm, hc);
    MEL_ASSERT_NOT_NULL(got);
    MEL_ASSERT_FLOAT_EQ(got->x, 3.0f, 0.001f);

    mel_slotmap_free(&sm);
}

MEL_TEST(storage_pool_dirty_resize, .tags = "gpu, render")
{
    Mel_BitSet dirty;
    mel_bitset_init(&dirty, 4, mel_alloc_heap());

    mel_bitset_set(&dirty, 0);
    mel_bitset_set(&dirty, 3);

    mel_bitset_resize(&dirty, 64);

    MEL_ASSERT(mel_bitset_get(&dirty, 0));
    MEL_ASSERT(mel_bitset_get(&dirty, 3));
    MEL_ASSERT(!mel_bitset_get(&dirty, 4));
    MEL_ASSERT(!mel_bitset_get(&dirty, 63));

    mel_bitset_set(&dirty, 63);
    MEL_ASSERT(mel_bitset_get(&dirty, 63));

    mel_bitset_free(&dirty);
}

MEL_TEST(storage_pool_bulk_insert_dirty, .tags = "gpu, render")
{
    Mel_SlotMap sm;
    Mel_BitSet dirty;

    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(Test_Storage_Item), .initial_capacity = 4);
    mel_bitset_init(&dirty, 4, mel_alloc_heap());

    for (u32 i = 0; i < 20; i++)
    {
        Test_Storage_Item item = { .x = (f32)i };
        mel_slotmap_insert(&sm, &item);

        if (sm.packed_count > dirty.bit_count)
            mel_bitset_resize(&dirty, sm.packed_capacity);

        mel_bitset_set(&dirty, sm.packed_count - 1);
    }

    MEL_ASSERT_EQ(mel_slotmap_count(&sm), (u32)20);
    MEL_ASSERT_EQ(mel_bitset_count_set(&dirty), (usize)20);

    mel_bitset_clear(&dirty);
    MEL_ASSERT(mel_bitset_none(&dirty));

    mel_slotmap_free(&sm);
    mel_bitset_free(&dirty);
}

MEL_TEST(storage_pool_alloc_free_churn, .tags = "gpu, render")
{
    Mel_SlotMap sm;
    Mel_BitSet dirty;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(Test_Storage_Item), .initial_capacity = 4);
    mel_bitset_init(&dirty, 4, mel_alloc_heap());

    Mel_SlotMap_Handle handles[50];

    for (u32 round = 0; round < 5; round++)
    {
        for (u32 i = 0; i < 50; i++)
        {
            Test_Storage_Item item = { .x = (f32)(round * 50 + i) };
            handles[i] = mel_slotmap_insert(&sm, &item);

            if (sm.packed_count > dirty.bit_count)
                mel_bitset_resize(&dirty, sm.packed_capacity);
            mel_bitset_set(&dirty, sm.packed_count - 1);
        }

        MEL_ASSERT_EQ(mel_slotmap_count(&sm), (u32)50);

        for (u32 i = 0; i < 50; i++)
        {
            MEL_ASSERT(mel_slotmap_alive(&sm, handles[i]));
            mel_slotmap_remove(&sm, handles[i]);
        }

        MEL_ASSERT_EQ(mel_slotmap_count(&sm), (u32)0);
        mel_bitset_clear(&dirty);
    }

    mel_slotmap_free(&sm);
    mel_bitset_free(&dirty);
}

MEL_TEST(storage_pool_dirty_clear_resets, .tags = "gpu, render")
{
    Mel_BitSet dirty;
    mel_bitset_init(&dirty, 32, mel_alloc_heap());

    for (u32 i = 0; i < 32; i++)
        mel_bitset_set(&dirty, i);

    MEL_ASSERT(mel_bitset_all(&dirty));
    MEL_ASSERT_EQ(mel_bitset_count_set(&dirty), (usize)32);

    mel_bitset_clear(&dirty);
    MEL_ASSERT(mel_bitset_none(&dirty));
    MEL_ASSERT_EQ(mel_bitset_count_set(&dirty), (usize)0);

    mel_bitset_set(&dirty, 5);
    MEL_ASSERT(mel_bitset_any(&dirty));
    MEL_ASSERT_EQ(mel_bitset_count_set(&dirty), (usize)1);
    MEL_ASSERT(mel_bitset_get(&dirty, 5));

    mel_bitset_free(&dirty);
}

MEL_TEST(storage_pool_generation_prevents_stale_access, .tags = "gpu, render")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(Test_Storage_Item), .initial_capacity = 4);

    Test_Storage_Item item = { .x = 1.0f };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&sm, &item);
    MEL_ASSERT(mel_slotmap_alive(&sm, h));

    mel_slotmap_remove(&sm, h);
    MEL_ASSERT(!mel_slotmap_alive(&sm, h));

    Test_Storage_Item item2 = { .x = 2.0f };
    Mel_SlotMap_Handle h2 = mel_slotmap_insert(&sm, &item2);

    MEL_ASSERT(!mel_slotmap_alive(&sm, h));
    MEL_ASSERT(mel_slotmap_alive(&sm, h2));

    Test_Storage_Item* got = mel_slotmap_get(&sm, h2);
    MEL_ASSERT_NOT_NULL(got);
    MEL_ASSERT_FLOAT_EQ(got->x, 2.0f, 0.001f);

    mel_slotmap_free(&sm);
}

MEL_TEST(storage_pool_set_data_updates_value, .tags = "gpu, render")
{
    Mel_SlotMap sm;
    Mel_BitSet dirty;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(Test_Storage_Item), .initial_capacity = 4);
    mel_bitset_init(&dirty, 4, mel_alloc_heap());

    Test_Storage_Item item = { .x = 1.0f, .y = 2.0f, .z = 3.0f, .w = 4.0f };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&sm, &item);

    Test_Storage_Item updated = { .x = 10.0f, .y = 20.0f, .z = 30.0f, .w = 40.0f };
    void* slot = mel_slotmap_get(&sm, h);
    memcpy(slot, &updated, sizeof(Test_Storage_Item));

    u32 packed_idx = sm.slots[h.index].packed_idx;
    mel_bitset_set(&dirty, packed_idx);

    MEL_ASSERT(mel_bitset_any(&dirty));

    Test_Storage_Item* got = mel_slotmap_get(&sm, h);
    MEL_ASSERT_FLOAT_EQ(got->x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(got->w, 40.0f, 0.001f);

    mel_slotmap_free(&sm);
    mel_bitset_free(&dirty);
}
