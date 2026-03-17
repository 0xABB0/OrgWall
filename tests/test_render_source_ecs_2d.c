#include "../melody/test.harness.h"
#include "../melody/render.source.ecs.2d.h"
#include "../melody/render.source.type.h"
#include "../melody/render.manager.2d.h"
#include "../melody/render.ecs.delta.h"
#include "../melody/ecs.2d.transform.h"
#include "../melody/ecs.2d.sprite.h"
#include "../melody/gpu.device.h"
#include "../melody/gpu.storage_pool.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.slotmap.fwd.h"

#include <SDL3/SDL.h>
#include <flecs.h>

static Mel_Gpu_Device s_dev;
static bool s_dev_ready = false;

static Mel_Gpu_Device* test_gpu_dev(void)
{
    if (!s_dev_ready)
    {
        SDL_Init(SDL_INIT_VIDEO);
        if (!mel_gpu_device_init(&s_dev, .allocator = mel_alloc_heap()))
            return nullptr;
        s_dev_ready = true;
    }
    return &s_dev;
}

MEL_TEST(source_ecs_2d_create_destroy, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(
        .world = world,
        .alloc = mel_alloc_heap());

    MEL_ASSERT_NOT_NULL(source);
    MEL_ASSERT(source->type == &mel_source_ecs_2d_type);

    mel_render_source_destroy(source);
    ecs_fini(world);
}

MEL_TEST(source_ecs_2d_sync_added, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(
        .world = world,
        .alloc = mel_alloc_heap());

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, Mel_CTransform, { .pos = mel_vec2(10.0f, 20.0f) });
    ecs_set(world, e1, Mel_Sprite, {
        .size = mel_vec2(32.0f, 32.0f),
        .color = mel_vec4(1, 0, 0, 1),
        .uv = mel_rect(0, 0, 1, 1),
    });

    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, Mel_CTransform, { .pos = mel_vec2(50.0f, 60.0f) });
    ecs_set(world, e2, Mel_Sprite, {
        .size = mel_vec2(16.0f, 16.0f),
        .color = mel_vec4(0, 1, 0, 1),
        .uv = mel_rect(0, 0, 0.5f, 0.5f),
    });

    mel_source_ecs_2d_sync(source, &mgr);

    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 2);

    Mel_Render_Handle_2D h1 = mel_source_ecs_2d_handle_for_entity(source, e1);
    MEL_ASSERT(mel_render_handle_2d_valid(h1));

    Mel_Render_Transform_2D* t1 = mel_storage_pool_get(&mgr.transforms, h1.handle);
    MEL_ASSERT_NOT_NULL(t1);
    MEL_ASSERT_FLOAT_EQ(t1->pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(t1->pos.y, 20.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(t1->scale.x, 32.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(t1->scale.y, 32.0f, 0.001f);

    Mel_Render_Sprite_Info* info1 = mel_storage_pool_get(&mgr.sprite_infos, h1.handle);
    MEL_ASSERT_NOT_NULL(info1);
    MEL_ASSERT_FLOAT_EQ(info1->color.r, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(info1->color.g, 0.0f, 0.001f);

    Mel_Render_Bounds_2D* b1 = mel_storage_pool_get(&mgr.bounds, h1.handle);
    MEL_ASSERT_NOT_NULL(b1);
    MEL_ASSERT_FLOAT_EQ(b1->center.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(b1->half_extents.x, 16.0f, 0.001f);

    Mel_Render_Handle_2D h2 = mel_source_ecs_2d_handle_for_entity(source, e2);
    MEL_ASSERT(mel_render_handle_2d_valid(h2));

    mel_mgr_2d_shutdown(&mgr);
    mel_render_source_destroy(source);
    ecs_fini(world);
}

MEL_TEST(source_ecs_2d_sync_modified, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(
        .world = world,
        .alloc = mel_alloc_heap());

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(10.0f, 20.0f) });
    ecs_set(world, e, Mel_Sprite, {
        .size = mel_vec2(32.0f, 32.0f),
        .color = mel_vec4(1, 1, 1, 1),
        .uv = mel_rect(0, 0, 1, 1),
    });

    mel_source_ecs_2d_sync(source, &mgr);
    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 1);

    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(100.0f, 200.0f) });

    mel_source_ecs_2d_sync(source, &mgr);
    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 1);

    Mel_Render_Handle_2D h = mel_source_ecs_2d_handle_for_entity(source, e);
    Mel_Render_Transform_2D* t = mel_storage_pool_get(&mgr.transforms, h.handle);
    MEL_ASSERT_NOT_NULL(t);
    MEL_ASSERT_FLOAT_EQ(t->pos.x, 100.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(t->pos.y, 200.0f, 0.001f);

    Mel_Render_Bounds_2D* b = mel_storage_pool_get(&mgr.bounds, h.handle);
    MEL_ASSERT_NOT_NULL(b);
    MEL_ASSERT_FLOAT_EQ(b->center.x, 100.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(b->center.y, 200.0f, 0.001f);

    mel_mgr_2d_shutdown(&mgr);
    mel_render_source_destroy(source);
    ecs_fini(world);
}

MEL_TEST(source_ecs_2d_sync_removed, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(
        .world = world,
        .alloc = mel_alloc_heap());

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, Mel_CTransform, { .pos = mel_vec2(10.0f, 20.0f) });
    ecs_set(world, e1, Mel_Sprite, {
        .size = mel_vec2(32.0f, 32.0f),
        .color = mel_vec4(1, 1, 1, 1),
        .uv = mel_rect(0, 0, 1, 1),
    });

    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, Mel_CTransform, { .pos = mel_vec2(50.0f, 60.0f) });
    ecs_set(world, e2, Mel_Sprite, {
        .size = mel_vec2(16.0f, 16.0f),
        .color = mel_vec4(0, 1, 0, 1),
        .uv = mel_rect(0, 0, 1, 1),
    });

    mel_source_ecs_2d_sync(source, &mgr);
    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 2);

    ecs_delete(world, e1);

    mel_source_ecs_2d_sync(source, &mgr);
    MEL_ASSERT_EQ(mel_mgr_2d_count(&mgr), 1);

    Mel_Render_Handle_2D h1 = mel_source_ecs_2d_handle_for_entity(source, e1);
    MEL_ASSERT(!mel_render_handle_2d_valid(h1));

    Mel_Render_Handle_2D h2 = mel_source_ecs_2d_handle_for_entity(source, e2);
    MEL_ASSERT(mel_render_handle_2d_valid(h2));

    mel_mgr_2d_shutdown(&mgr);
    mel_render_source_destroy(source);
    ecs_fini(world);
}

MEL_TEST(source_ecs_2d_handle_lookup, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(
        .world = world,
        .alloc = mel_alloc_heap());

    Mel_Render_Manager_2D mgr = {0};
    mel_mgr_2d_init(&mgr, .dev = dev, .alloc = mel_alloc_heap());

    Mel_Render_Handle_2D before = mel_source_ecs_2d_handle_for_entity(source, 9999);
    MEL_ASSERT(!mel_render_handle_2d_valid(before));

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(5.0f, 5.0f) });
    ecs_set(world, e, Mel_Sprite, {
        .size = mel_vec2(8.0f, 8.0f),
        .color = mel_vec4(1, 1, 1, 1),
        .uv = mel_rect(0, 0, 1, 1),
    });

    mel_source_ecs_2d_sync(source, &mgr);

    Mel_Render_Handle_2D after = mel_source_ecs_2d_handle_for_entity(source, e);
    MEL_ASSERT(mel_render_handle_2d_valid(after));

    ecs_delete(world, e);
    mel_source_ecs_2d_sync(source, &mgr);

    Mel_Render_Handle_2D gone = mel_source_ecs_2d_handle_for_entity(source, e);
    MEL_ASSERT(!mel_render_handle_2d_valid(gone));

    mel_mgr_2d_shutdown(&mgr);
    mel_render_source_destroy(source);
    ecs_fini(world);
}
