#include "collection.slotmap.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <stdio.h>

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  %-50s", #name); name(); printf("PASS\n"); g_pass++; } while(0)
#define ASSERT(cond) do { if (!(cond)) { printf("FAIL (%s:%d: %s)\n", __FILE__, __LINE__, #cond); g_fail++; return; } } while(0)

typedef struct {
    u32 a;
    u32 b;
} TestItem;

TEST(test_insert_and_get)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 4);

    TestItem item = { .a = 42, .b = 99 };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&sm, &item);

    ASSERT(mel_slotmap_handle_valid(h));
    ASSERT(mel_slotmap_alive(&sm, h));

    TestItem* got = mel_slotmap_get(&sm, h);
    ASSERT(got != nullptr);
    ASSERT(got->a == 42);
    ASSERT(got->b == 99);

    mel_slotmap_free(&sm);
}

TEST(test_remove_invalidates)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 4);

    TestItem item = { .a = 1, .b = 2 };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&sm, &item);

    ASSERT(mel_slotmap_remove(&sm, h));
    ASSERT(!mel_slotmap_alive(&sm, h));
    ASSERT(mel_slotmap_get(&sm, h) == nullptr);
    ASSERT(!mel_slotmap_remove(&sm, h));

    mel_slotmap_free(&sm);
}

TEST(test_generation_bump_on_reuse)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 4);

    TestItem item1 = { .a = 10, .b = 20 };
    Mel_SlotMap_Handle h1 = mel_slotmap_insert(&sm, &item1);
    u32 idx1 = mel_slotmap_handle_index(h1);

    mel_slotmap_remove(&sm, h1);

    TestItem item2 = { .a = 30, .b = 40 };
    Mel_SlotMap_Handle h2 = mel_slotmap_insert(&sm, &item2);
    u32 idx2 = mel_slotmap_handle_index(h2);

    ASSERT(idx1 == idx2);
    ASSERT(mel_slotmap_handle_gen(h2) == mel_slotmap_handle_gen(h1) + 1);
    ASSERT(mel_slotmap_get(&sm, h1) == nullptr);

    TestItem* got = mel_slotmap_get(&sm, h2);
    ASSERT(got != nullptr);
    ASSERT(got->a == 30);
    ASSERT(got->b == 40);

    mel_slotmap_free(&sm);
}

TEST(test_packed_stays_contiguous)
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

    ASSERT(mel_slotmap_count(&sm) == 3);

    TestItem* data = mel_slotmap_data(&sm);
    bool found[4] = {false};
    for (u32 i = 0; i < mel_slotmap_count(&sm); i++)
    {
        TestItem* d = (TestItem*)((u8*)data + i * sizeof(TestItem));
        found[d->a] = true;
    }
    ASSERT(found[0]);
    ASSERT(!found[1]);
    ASSERT(found[2]);
    ASSERT(found[3]);

    mel_slotmap_free(&sm);
}

TEST(test_growth_past_capacity)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 2);

    Mel_SlotMap_Handle handles[20];
    for (int i = 0; i < 20; i++)
    {
        TestItem item = { .a = (u32)i, .b = (u32)(i * 10) };
        handles[i] = mel_slotmap_insert(&sm, &item);
    }

    ASSERT(mel_slotmap_count(&sm) == 20);

    for (int i = 0; i < 20; i++)
    {
        TestItem* got = mel_slotmap_get(&sm, handles[i]);
        ASSERT(got != nullptr);
        ASSERT(got->a == (u32)i);
        ASSERT(got->b == (u32)(i * 10));
    }

    mel_slotmap_free(&sm);
}

TEST(test_null_handle_returns_null)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 4);

    ASSERT(mel_slotmap_get(&sm, MEL_SLOTMAP_HANDLE_NULL) == nullptr);
    ASSERT(!mel_slotmap_alive(&sm, MEL_SLOTMAP_HANDLE_NULL));
    ASSERT(!mel_slotmap_remove(&sm, MEL_SLOTMAP_HANDLE_NULL));

    mel_slotmap_free(&sm);
}

TEST(test_iteration_count_matches)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(TestItem), .initial_capacity = 8);

    for (int i = 0; i < 10; i++)
    {
        TestItem item = { .a = (u32)i, .b = 0 };
        mel_slotmap_insert(&sm, &item);
    }

    ASSERT(mel_slotmap_count(&sm) == 10);

    TestItem* data = mel_slotmap_data(&sm);
    u32 sum = 0;
    for (u32 i = 0; i < mel_slotmap_count(&sm); i++)
    {
        TestItem* d = (TestItem*)((u8*)data + i * sizeof(TestItem));
        sum += d->a;
    }
    ASSERT(sum == 45);

    mel_slotmap_free(&sm);
}

TEST(test_handle_pack_unpack)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_pack(12345, 678);
    ASSERT(mel_slotmap_handle_index(h) == 12345);
    ASSERT(mel_slotmap_handle_gen(h) == 678);

    Mel_SlotMap_Handle max = mel_slotmap_handle_pack(MEL_SLOTMAP_MAX_INDEX, MEL_SLOTMAP_MAX_GEN);
    ASSERT(mel_slotmap_handle_index(max) == MEL_SLOTMAP_MAX_INDEX);
    ASSERT(mel_slotmap_handle_gen(max) == MEL_SLOTMAP_MAX_GEN);
}

int main(void)
{
    printf("collection.slotmap tests:\n");

    RUN(test_insert_and_get);
    RUN(test_remove_invalidates);
    RUN(test_generation_bump_on_reuse);
    RUN(test_packed_stays_contiguous);
    RUN(test_growth_past_capacity);
    RUN(test_null_handle_returns_null);
    RUN(test_iteration_count_matches);
    RUN(test_handle_pack_unpack);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
