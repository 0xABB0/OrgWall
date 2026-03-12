#include "../melody/test.harness.h"
#include "../melody/render.list.h"
#include "../melody/allocator.heap.h"
#include <string.h>

typedef struct {
    f32 x, y;
    u32 color;
} Test_Entry;

MEL_TEST(list_init_shutdown, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());
    MEL_ASSERT_EQ(list.count, 0u);
    MEL_ASSERT_EQ(list.slot_count, 0u);
    MEL_ASSERT_EQ(list.entry_stride, (u32)sizeof(Test_Entry));
    mel_render_list_shutdown(&list);
}

MEL_TEST(list_insert_and_get, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());

    u32 idx = mel_render_list_insert(&list, 100);
    MEL_ASSERT_EQ(list.count, 1u);

    Test_Entry* e = mel_render_list_get(&list, idx);
    MEL_ASSERT_NOT_NULL(e);
    e->x = 1.0f;
    e->y = 2.0f;
    e->color = 0xFF00FF00;

    Test_Entry* e2 = mel_render_list_get(&list, idx);
    MEL_ASSERT_FLOAT_EQ(e2->x, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(e2->y, 2.0f, 0.001f);
    MEL_ASSERT_EQ(e2->color, 0xFF00FF00u);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_push_returns_pointer, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());

    Test_Entry* e = mel_render_list_push(&list, 50);
    MEL_ASSERT_NOT_NULL(e);
    e->x = 42.0f;
    e->y = 99.0f;

    MEL_ASSERT_EQ(list.count, 1u);

    Test_Entry* e2 = mel_render_list_get(&list, 0);
    MEL_ASSERT_FLOAT_EQ(e2->x, 42.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(e2->y, 99.0f, 0.001f);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_remove_and_reuse, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());

    u32 a = mel_render_list_insert(&list, 10);
    u32 b = mel_render_list_insert(&list, 20);
    MEL_ASSERT_EQ(list.count, 2u);

    mel_render_list_remove(&list, a);
    MEL_ASSERT_EQ(list.count, 1u);
    MEL_ASSERT_EQ(list.free_count, 1u);

    u32 c = mel_render_list_insert(&list, 30);
    MEL_ASSERT_EQ(c, a);
    MEL_ASSERT_EQ(list.count, 2u);
    MEL_ASSERT_EQ(list.free_count, 0u);

    MEL_UNUSED(b);
    mel_render_list_shutdown(&list);
}

MEL_TEST(list_sort_ordering, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(u32), .alloc = mel_alloc_heap());

    u32 idx_c = mel_render_list_insert(&list, 300);
    u32 idx_a = mel_render_list_insert(&list, 100);
    u32 idx_b = mel_render_list_insert(&list, 200);

    *(u32*)mel_render_list_get(&list, idx_c) = 0xCC;
    *(u32*)mel_render_list_get(&list, idx_a) = 0xAA;
    *(u32*)mel_render_list_get(&list, idx_b) = 0xBB;

    mel_render_list_sort(&list);
    MEL_ASSERT(!list.dirty);

    MEL_ASSERT_EQ(list.packets[0].sort_key, (u64)100);
    MEL_ASSERT_EQ(list.packets[1].sort_key, (u64)200);
    MEL_ASSERT_EQ(list.packets[2].sort_key, (u64)300);

    u32 val0 = *(u32*)mel_render_list_get(&list, list.packets[0].entry_index);
    u32 val1 = *(u32*)mel_render_list_get(&list, list.packets[1].entry_index);
    u32 val2 = *(u32*)mel_render_list_get(&list, list.packets[2].entry_index);
    MEL_ASSERT_EQ(val0, 0xAAu);
    MEL_ASSERT_EQ(val1, 0xBBu);
    MEL_ASSERT_EQ(val2, 0xCCu);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_clear, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(u32), .alloc = mel_alloc_heap());

    mel_render_list_insert(&list, 1);
    mel_render_list_insert(&list, 2);
    mel_render_list_insert(&list, 3);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_clear(&list);
    MEL_ASSERT_EQ(list.count, 0u);
    MEL_ASSERT_EQ(list.slot_count, 0u);
    MEL_ASSERT_EQ(list.free_count, 0u);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_growth, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(u64), .alloc = mel_alloc_heap());

    for (u32 i = 0; i < 100; i++)
    {
        u32 idx = mel_render_list_insert(&list, (u64)i);
        *(u64*)mel_render_list_get(&list, idx) = (u64)(i * 10);
    }

    MEL_ASSERT_EQ(list.count, 100u);
    MEL_ASSERT_GE(list.capacity, 100u);

    for (u32 i = 0; i < 100; i++)
    {
        u64 val = *(u64*)mel_render_list_get(&list, i);
        MEL_ASSERT_EQ(val, (u64)(i * 10));
    }

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_update_key, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(u32), .alloc = mel_alloc_heap());

    u32 a = mel_render_list_insert(&list, 300);
    u32 b = mel_render_list_insert(&list, 100);
    u32 c = mel_render_list_insert(&list, 200);

    *(u32*)mel_render_list_get(&list, a) = 0xAA;
    *(u32*)mel_render_list_get(&list, b) = 0xBB;
    *(u32*)mel_render_list_get(&list, c) = 0xCC;

    mel_render_list_update_key(&list, a, 50);
    MEL_ASSERT(list.dirty);

    mel_render_list_sort(&list);

    MEL_ASSERT_EQ(list.packets[0].sort_key, (u64)50);
    MEL_ASSERT_EQ(list.packets[0].entry_index, a);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_mixed_ops, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(u32), .alloc = mel_alloc_heap());

    u32 a = mel_render_list_insert(&list, 10);
    u32 b = mel_render_list_insert(&list, 20);
    u32 c = mel_render_list_insert(&list, 30);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_remove(&list, b);
    MEL_ASSERT_EQ(list.count, 2u);

    u32 d = mel_render_list_insert(&list, 15);
    MEL_ASSERT_EQ(d, b);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_sort(&list);

    MEL_ASSERT_EQ(list.packets[0].sort_key, (u64)10);
    MEL_ASSERT_EQ(list.packets[1].sort_key, (u64)15);
    MEL_ASSERT_EQ(list.packets[2].sort_key, (u64)30);

    MEL_UNUSED(a);
    MEL_UNUSED(c);
    mel_render_list_shutdown(&list);
}

MEL_TEST(list_sort_stability, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(u32), .alloc = mel_alloc_heap());

    u32 a = mel_render_list_insert(&list, 100);
    u32 b = mel_render_list_insert(&list, 100);
    u32 c = mel_render_list_insert(&list, 100);

    mel_render_list_sort(&list);

    MEL_ASSERT_EQ(list.packets[0].entry_index, a);
    MEL_ASSERT_EQ(list.packets[1].entry_index, b);
    MEL_ASSERT_EQ(list.packets[2].entry_index, c);

    mel_render_list_shutdown(&list);
}

static void test_producer_push_three(Mel_Render_List* list, void* user)
{
    u32* call_count = (u32*)user;
    (*call_count)++;

    for (u32 i = 0; i < 3; i++)
    {
        Test_Entry* e = mel_render_list_push(list, (u64)(i + 1));
        e->x = (f32)i;
        e->y = (f32)(i * 10);
        e->color = 0xFFu;
    }
}

MEL_TEST(list_producer_basic, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());

    u32 call_count = 0;
    mel_render_list_add_producer(&list, test_producer_push_three, &call_count);
    MEL_ASSERT_EQ(list.producer_count, 1u);

    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(call_count, 1u);
    MEL_ASSERT_EQ(list.count, 3u);
    MEL_ASSERT(list.produced);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_producer_idempotent, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());

    u32 call_count = 0;
    mel_render_list_add_producer(&list, test_producer_push_three, &call_count);

    mel_render_list_produce(&list);
    mel_render_list_produce(&list);
    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(call_count, 1u);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_clear(&list);
    MEL_ASSERT(!list.produced);

    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(call_count, 2u);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_begin_frame_retained_keeps_entries_and_resets_produced, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());

    u32 call_count = 0;
    mel_render_list_add_producer(&list, test_producer_push_three, &call_count);

    mel_render_list_begin_frame(&list, 7);
    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(call_count, 1u);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_begin_frame(&list, 7);
    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(call_count, 1u);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_begin_frame(&list, 8);
    MEL_ASSERT(!list.produced);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_begin_frame_ephemeral_clears_entries, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = mel_alloc_heap());

    u32 call_count = 0;
    mel_render_list_add_producer(&list, test_producer_push_three, &call_count);

    mel_render_list_begin_frame(&list, 11);
    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(call_count, 1u);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_begin_frame(&list, 12);
    MEL_ASSERT_EQ(list.count, 0u);
    MEL_ASSERT_EQ(list.slot_count, 0u);
    MEL_ASSERT(!list.produced);

    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(call_count, 2u);
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_producer_remove, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());

    u32 call_count = 0;
    mel_render_list_add_producer(&list, test_producer_push_three, &call_count);
    mel_render_list_remove_producer(&list, test_producer_push_three);
    MEL_ASSERT_EQ(list.producer_count, 0u);

    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(call_count, 0u);
    MEL_ASSERT_EQ(list.count, 0u);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_producer_no_producers_is_noop, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list, .entry_stride = sizeof(Test_Entry), .alloc = mel_alloc_heap());

    mel_render_list_produce(&list);
    MEL_ASSERT_EQ(list.count, 0u);
    MEL_ASSERT(!list.produced);

    mel_render_list_shutdown(&list);
}

MEL_TEST(list_initial_capacity, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(u32),
        .initial_capacity = 64,
        .alloc = mel_alloc_heap(),
    );

    MEL_ASSERT_EQ(list.capacity, 64u);
    MEL_ASSERT_NOT_NULL(list.entries);
    MEL_ASSERT_NOT_NULL(list.packets);

    for (u32 i = 0; i < 64; i++)
        mel_render_list_insert(&list, (u64)i);

    MEL_ASSERT_EQ(list.count, 64u);
    MEL_ASSERT_EQ(list.capacity, 64u);

    mel_render_list_shutdown(&list);
}
