#include "../melody/test.harness.h"
#include "../melody/collection.slotmap.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"

typedef struct {
    u32 a;
    u32 b;
} TestItem;

MEL_TEST(insert_and_get, .tags = "collection")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 4);

    TestItem item = { .a = 42, .b = 99 };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&sm, &item);

    MEL_ASSERT(mel_slotmap_handle_valid(h));
    MEL_ASSERT(mel_slotmap_alive(&sm, h));

    TestItem* got = mel_slotmap_get(&sm, h);
    MEL_ASSERT_NOT_NULL(got);
    MEL_ASSERT_EQ(got->a, 42u);
    MEL_ASSERT_EQ(got->b, 99u);

    mel_slotmap_free(&sm);
}

MEL_TEST(slotmap_remove_invalidates, .tags = "collection")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 4);

    TestItem item = { .a = 1, .b = 2 };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&sm, &item);

    MEL_ASSERT(mel_slotmap_remove(&sm, h));
    MEL_ASSERT(!mel_slotmap_alive(&sm, h));
    MEL_ASSERT_NULL(mel_slotmap_get(&sm, h));
    MEL_ASSERT(!mel_slotmap_remove(&sm, h));

    mel_slotmap_free(&sm);
}

MEL_TEST(generation_bump_on_reuse, .tags = "collection")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 4);

    TestItem item1 = { .a = 10, .b = 20 };
    Mel_SlotMap_Handle h1 = mel_slotmap_insert(&sm, &item1);
    u32 idx1 = h1.index;

    mel_slotmap_remove(&sm, h1);

    TestItem item2 = { .a = 30, .b = 40 };
    Mel_SlotMap_Handle h2 = mel_slotmap_insert(&sm, &item2);
    u32 idx2 = h2.index;

    MEL_ASSERT_EQ(idx1, idx2);
    MEL_ASSERT_EQ(h2.generation, h1.generation + 1);
    MEL_ASSERT_NULL(mel_slotmap_get(&sm, h1));

    TestItem* got = mel_slotmap_get(&sm, h2);
    MEL_ASSERT_NOT_NULL(got);
    MEL_ASSERT_EQ(got->a, 30u);
    MEL_ASSERT_EQ(got->b, 40u);

    mel_slotmap_free(&sm);
}

MEL_TEST(packed_stays_contiguous, .tags = "collection")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 8);

    TestItem items[4] = {
        { .a = 0, .b = 100 },
        { .a = 1, .b = 101 },
        { .a = 2, .b = 102 },
        { .a = 3, .b = 103 },
    };

    Mel_SlotMap_Handle handles[4];
    for (int i = 0; i < 4; i++)
        handles[i] = mel_slotmap_insert(&sm, &items[i]);

    mel_slotmap_remove(&sm, handles[1]);

    MEL_ASSERT_EQ(mel_slotmap_count(&sm), 3u);

    TestItem* data = mel_slotmap_data(&sm);
    bool found[4] = {false};
    for (u32 i = 0; i < mel_slotmap_count(&sm); i++)
    {
        TestItem* d = (TestItem*)((u8*)data + i * sizeof(TestItem));
        found[d->a] = true;
    }
    MEL_ASSERT(found[0]);
    MEL_ASSERT(!found[1]);
    MEL_ASSERT(found[2]);
    MEL_ASSERT(found[3]);

    mel_slotmap_free(&sm);
}

MEL_TEST(slotmap_growth_past_capacity, .tags = "collection")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 2);

    Mel_SlotMap_Handle handles[20];
    for (int i = 0; i < 20; i++)
    {
        TestItem item = { .a = (u32)i, .b = (u32)(i * 10) };
        handles[i] = mel_slotmap_insert(&sm, &item);
    }

    MEL_ASSERT_EQ(mel_slotmap_count(&sm), 20u);

    for (int i = 0; i < 20; i++)
    {
        TestItem* got = mel_slotmap_get(&sm, handles[i]);
        MEL_ASSERT_NOT_NULL(got);
        MEL_ASSERT_EQ(got->a, (u32)i);
        MEL_ASSERT_EQ(got->b, (u32)(i * 10));
    }

    mel_slotmap_free(&sm);
}

MEL_TEST(null_handle_returns_null, .tags = "collection")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 4);

    MEL_ASSERT_NULL(mel_slotmap_get(&sm, MEL_SLOTMAP_HANDLE_NULL));
    MEL_ASSERT(!mel_slotmap_alive(&sm, MEL_SLOTMAP_HANDLE_NULL));
    MEL_ASSERT(!mel_slotmap_remove(&sm, MEL_SLOTMAP_HANDLE_NULL));

    mel_slotmap_free(&sm);
}

MEL_TEST(iteration_count_matches, .tags = "collection")
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 8);

    for (int i = 0; i < 10; i++)
    {
        TestItem item = { .a = (u32)i, .b = 0 };
        mel_slotmap_insert(&sm, &item);
    }

    MEL_ASSERT_EQ(mel_slotmap_count(&sm), 10u);

    TestItem* data = mel_slotmap_data(&sm);
    u32 sum = 0;
    for (u32 i = 0; i < mel_slotmap_count(&sm); i++)
    {
        TestItem* d = (TestItem*)((u8*)data + i * sizeof(TestItem));
        sum += d->a;
    }
    MEL_ASSERT_EQ(sum, 45u);

    mel_slotmap_free(&sm);
}

MEL_TEST(handle_fields, .tags = "collection")
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_make(12345, 678);
    MEL_ASSERT_EQ(h.index, 12345u);
    MEL_ASSERT_EQ(h.generation, 678u);

    u64 packed = mel_slotmap_handle_pack64(h);
    Mel_SlotMap_Handle unpacked = mel_slotmap_handle_unpack64(packed);
    MEL_ASSERT_EQ(unpacked.index, 12345u);
    MEL_ASSERT_EQ(unpacked.generation, 678u);

    void* ptr = mel_slotmap_handle_to_ptr(h);
    Mel_SlotMap_Handle from_ptr = mel_slotmap_handle_from_ptr(ptr);
    MEL_ASSERT_EQ(from_ptr.index, 12345u);
    MEL_ASSERT_EQ(from_ptr.generation, 678u);
}
