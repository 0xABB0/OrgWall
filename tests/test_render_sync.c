#include "../melody/test.harness.h"
#include "../melody/render.sync.h"
#include "../melody/render.list.h"
#include "../melody/allocator.heap.h"
#include "../melody/ecs.world.h"

typedef struct { f32 x, y; } Test_Pos;
typedef struct { f32 r, g, b, a; } Test_Color;
typedef struct { u64 key; } Test_Order;

typedef struct {
    f32 x, y;
    f32 r, g, b, a;
} Test_Sync_Entry;

ECS_COMPONENT_DECLARE(Test_Pos);
ECS_COMPONENT_DECLARE(Test_Color);
ECS_COMPONENT_DECLARE(Test_Order);

static void test_write(void* entry_ptr, ecs_iter_t* it, i32 row, void* user)
{
    (void)user;
    Test_Pos* pos = ecs_field(it, Test_Pos, 0);
    Test_Color* col = ecs_field(it, Test_Color, 1);
    Test_Sync_Entry* entry = entry_ptr;
    entry->x = pos[row].x;
    entry->y = pos[row].y;
    entry->r = col[row].r;
    entry->g = col[row].g;
    entry->b = col[row].b;
    entry->a = col[row].a;
}

static u64 test_key(ecs_iter_t* it, i32 row, void* user)
{
    (void)user;
    Test_Order* order = ecs_field(it, Test_Order, 2);
    return order[row].key;
}

MEL_TEST(sync_add_entity, .tags = "render")
{
    Mel_ECS ecs;
    mel_ecs_init(&ecs);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Color);

    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    ecs_entity_t e = ecs_new(ecs.world);
    ecs_set(ecs.world, e, Test_Pos, { .x = 10.0f, .y = 20.0f });
    ecs_set(ecs.world, e, Test_Color, { .r = 1.0f, .g = 0.5f, .b = 0.0f, .a = 1.0f });

    MEL_ASSERT_EQ(list.count, 1u);

    void* val = mel_hashmap_get(&sync.entity_map, (void*)(usize)e);
    MEL_ASSERT_NOT_NULL(val);
    u32 entry_index = (u32)((usize)val - 1);

    Test_Sync_Entry* entry = mel_render_list_get(&list, entry_index);
    MEL_ASSERT_FLOAT_EQ(entry->x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(entry->y, 20.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(entry->r, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(entry->g, 0.5f, 0.001f);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs);
}

MEL_TEST(sync_remove_entity, .tags = "render")
{
    Mel_ECS ecs;
    mel_ecs_init(&ecs);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Color);

    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    ecs_entity_t e = ecs_new(ecs.world);
    ecs_set(ecs.world, e, Test_Pos, { .x = 1.0f, .y = 2.0f });
    ecs_set(ecs.world, e, Test_Color, { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f });
    MEL_ASSERT_EQ(list.count, 1u);

    ecs_delete(ecs.world, e);
    MEL_ASSERT_EQ(list.count, 0u);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs);
}

MEL_TEST(sync_update_positions, .tags = "render")
{
    Mel_ECS ecs;
    mel_ecs_init(&ecs);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Color);

    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    ecs_entity_t e = ecs_new(ecs.world);
    ecs_set(ecs.world, e, Test_Pos, { .x = 0.0f, .y = 0.0f });
    ecs_set(ecs.world, e, Test_Color, { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f });

    void* val = mel_hashmap_get(&sync.entity_map, (void*)(usize)e);
    u32 entry_index = (u32)((usize)val - 1);
    Test_Sync_Entry* entry = mel_render_list_get(&list, entry_index);
    MEL_ASSERT_FLOAT_EQ(entry->x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(entry->y, 0.0f, 0.001f);

    Test_Pos* pos = ecs_get_mut(ecs.world, e, Test_Pos);
    pos->x = 42.0f;
    pos->y = 99.0f;

    mel_render_sync_update(&sync);

    MEL_ASSERT_FLOAT_EQ(entry->x, 42.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(entry->y, 99.0f, 0.001f);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs);
}

MEL_TEST(sync_on_set_updates_existing_entry, .tags = "render")
{
    Mel_ECS ecs;
    mel_ecs_init(&ecs);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Color);

    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    ecs_entity_t e = ecs_new(ecs.world);
    ecs_set(ecs.world, e, Test_Pos, { .x = 1.0f, .y = 2.0f });
    ecs_set(ecs.world, e, Test_Color, { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f });

    void* val = mel_hashmap_get(&sync.entity_map, (void*)(usize)e);
    MEL_ASSERT_NOT_NULL(val);
    u32 entry_index = (u32)((usize)val - 1);
    Test_Sync_Entry* entry = mel_render_list_get(&list, entry_index);
    MEL_ASSERT_FLOAT_EQ(entry->x, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(entry->y, 2.0f, 0.001f);

    ecs_set(ecs.world, e, Test_Pos, { .x = 42.0f, .y = 99.0f });

    MEL_ASSERT_FLOAT_EQ(entry->x, 42.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(entry->y, 99.0f, 0.001f);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs);
}

MEL_TEST(sync_update_recomputes_sort_key, .tags = "render")
{
    Mel_ECS ecs;
    mel_ecs_init(&ecs);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Color);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Order);

    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color), ecs_id(Test_Order) },
        .write = test_write,
        .key = test_key);

    ecs_entity_t e = ecs_new(ecs.world);
    ecs_set(ecs.world, e, Test_Pos, { .x = 0.0f, .y = 0.0f });
    ecs_set(ecs.world, e, Test_Color, { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f });
    ecs_set(ecs.world, e, Test_Order, { .key = 10 });

    void* val = mel_hashmap_get(&sync.entity_map, (void*)(usize)e);
    MEL_ASSERT_NOT_NULL(val);
    u32 entry_index = (u32)((usize)val - 1);
    u32 packet_index = list.packet_map[entry_index];
    MEL_ASSERT_EQ(list.packets[packet_index].sort_key, (u64)10);

    ecs_set(ecs.world, e, Test_Order, { .key = 200 });
    mel_render_sync_update(&sync);

    packet_index = list.packet_map[entry_index];
    MEL_ASSERT_EQ(list.packets[packet_index].sort_key, (u64)200);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs);
}

MEL_TEST(sync_yield_existing, .tags = "render")
{
    Mel_ECS ecs;
    mel_ecs_init(&ecs);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Color);

    for (i32 i = 0; i < 3; i++)
    {
        ecs_entity_t e = ecs_new(ecs.world);
        ecs_set(ecs.world, e, Test_Pos, { .x = (f32)i, .y = (f32)(i * 10) });
        ecs_set(ecs.world, e, Test_Color, { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f });
    }

    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    MEL_ASSERT_EQ(list.count, 3u);
    MEL_ASSERT_EQ(mel_hashmap_count(&sync.entity_map), (usize)3);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs);
}

MEL_TEST(sync_multiple_entities, .tags = "render")
{
    Mel_ECS ecs;
    mel_ecs_init(&ecs);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Color);

    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    ecs_entity_t entities[5];
    for (i32 i = 0; i < 5; i++)
    {
        entities[i] = ecs_new(ecs.world);
        ecs_set(ecs.world, entities[i], Test_Pos, { .x = (f32)i, .y = (f32)i });
        ecs_set(ecs.world, entities[i], Test_Color, { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f });
    }
    MEL_ASSERT_EQ(list.count, 5u);

    ecs_delete(ecs.world, entities[1]);
    ecs_delete(ecs.world, entities[3]);
    MEL_ASSERT_EQ(list.count, 3u);
    MEL_ASSERT_EQ(mel_hashmap_count(&sync.entity_map), (usize)3);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs);
}

MEL_TEST(sync_restart_lifecycle, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_ECS ecs_a;
    mel_ecs_init(&ecs_a);
    ECS_COMPONENT_DEFINE(ecs_a.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs_a.world, Test_Color);

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs_a.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    for (i32 i = 0; i < 3; i++)
    {
        ecs_entity_t e = ecs_new(ecs_a.world);
        ecs_set(ecs_a.world, e, Test_Pos, { .x = (f32)i, .y = 0.0f });
        ecs_set(ecs_a.world, e, Test_Color, { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f });
    }
    MEL_ASSERT_EQ(list.count, 3u);

    mel_render_sync_shutdown(&sync);
    MEL_ASSERT_EQ(list.count, 0u);
    mel_ecs_shutdown(&ecs_a);

    Mel_ECS ecs_b;
    mel_ecs_init(&ecs_b);
    ECS_COMPONENT_DEFINE(ecs_b.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs_b.world, Test_Color);

    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs_b.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    for (i32 i = 0; i < 2; i++)
    {
        ecs_entity_t e = ecs_new(ecs_b.world);
        ecs_set(ecs_b.world, e, Test_Pos, { .x = (f32)(i + 100), .y = 0.0f });
        ecs_set(ecs_b.world, e, Test_Color, { .r = 0.0f, .g = 1.0f, .b = 0.0f, .a = 1.0f });
    }
    MEL_ASSERT_EQ(list.count, 2u);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs_b);
}

MEL_TEST(sync_partial_components, .tags = "render")
{
    Mel_ECS ecs;
    mel_ecs_init(&ecs);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Pos);
    ECS_COMPONENT_DEFINE(ecs.world, Test_Color);

    Mel_Render_List list;
    mel_render_list_init(&list,
        .entry_stride = sizeof(Test_Sync_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_Sync sync;
    mel_render_sync_init(&sync,
        .list = &list,
        .world = ecs.world,
        .components = { ecs_id(Test_Pos), ecs_id(Test_Color) },
        .write = test_write);

    ecs_entity_t e = ecs_new(ecs.world);
    ecs_set(ecs.world, e, Test_Pos, { .x = 10.0f, .y = 20.0f });
    MEL_ASSERT_EQ(list.count, 0u);

    ecs_set(ecs.world, e, Test_Color, { .r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f });
    MEL_ASSERT_EQ(list.count, 1u);

    mel_render_sync_shutdown(&sync);
    mel_render_list_shutdown(&list);
    mel_ecs_shutdown(&ecs);
}
