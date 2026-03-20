#include "../melody/test.harness.h"
#include "../melody/render.source.ecs.2d.h"
#include "../melody/render.scene.h"
#include "../melody/render.manager.h"
#include "../melody/render.types.2d.h"
#include "../melody/ecs.2d.transform.h"
#include "../melody/ecs.2d.sprite.h"
#include "../melody/gpu.device.h"
#include "../melody/allocator.heap.h"

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

static Mel_Render_Scene* make_scene(Mel_Gpu_Device* dev)
{
    return mel_render_scene_create(
        .dev = dev,
        .alloc = mel_alloc_heap());
}

MEL_TEST(source_ecs_2d_create_destroy, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(.world = world, .alloc = mel_alloc_heap());
    Mel_Render_Scene* scene = make_scene(dev);
    mel_render_scene_attach_source(scene, source);
    MEL_ASSERT_NOT_NULL(source);
    MEL_ASSERT(source->type == &mel_source_ecs_2d_type);

    mel_render_source_destroy(source);
    mel_render_scene_destroy(scene);
    ecs_fini(world);
}

MEL_TEST(source_ecs_2d_sync_added, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(.world = world, .alloc = mel_alloc_heap());
    Mel_Render_Scene* scene = make_scene(dev);
    mel_render_scene_attach_source(scene, source);

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

    mel_render_scene_sync(scene);
    Mel_Render_Manager* mgr = mel_render_scene_manager(scene);

    MEL_ASSERT_EQ(mel_mgr_count(mgr), 2);

    Mel_Render_Handle h1 = mel_source_ecs_2d_handle_for_entity(source, e1);
    MEL_ASSERT(mel_render_handle_valid(h1));

    Mel_Render_Instance* i1 = mel_mgr_get_instance(mgr, h1);
    MEL_ASSERT_NOT_NULL(i1);
    MEL_ASSERT_EQ(i1->source, source);

    Mel_Render_Transform_2D t1 = {0};
    Mel_Render_Sprite_Info s1 = {0};
    MEL_ASSERT(mel_source_ecs_2d_get_sprite_payload(source, h1, &t1, &s1));
    MEL_ASSERT_FLOAT_EQ(t1.pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(t1.pos.y, 20.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(t1.scale.x, 32.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(s1.color.r, 1.0f, 0.001f);

    Mel_Render_Handle h2 = mel_source_ecs_2d_handle_for_entity(source, e2);
    MEL_ASSERT(mel_render_handle_valid(h2));

    mel_render_source_destroy(source);
    mel_render_scene_destroy(scene);
    ecs_fini(world);
}

MEL_TEST(source_ecs_2d_sync_modified, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(.world = world, .alloc = mel_alloc_heap());
    Mel_Render_Scene* scene = make_scene(dev);
    mel_render_scene_attach_source(scene, source);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(10.0f, 20.0f) });
    ecs_set(world, e, Mel_Sprite, {
        .size = mel_vec2(32.0f, 32.0f),
        .color = mel_vec4(1, 1, 1, 1),
        .uv = mel_rect(0, 0, 1, 1),
    });

    mel_render_scene_sync(scene);
    Mel_Render_Manager* mgr = mel_render_scene_manager(scene);
    MEL_ASSERT_EQ(mel_mgr_count(mgr), 1);

    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(100.0f, 200.0f) });

    mel_render_scene_sync(scene);
    MEL_ASSERT_EQ(mel_mgr_count(mgr), 1);

    Mel_Render_Handle h = mel_source_ecs_2d_handle_for_entity(source, e);
    Mel_Render_Transform_2D t = {0};
    MEL_ASSERT(mel_source_ecs_2d_get_sprite_payload(source, h, &t, nullptr));
    MEL_ASSERT_FLOAT_EQ(t.pos.x, 100.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(t.pos.y, 200.0f, 0.001f);

    mel_render_source_destroy(source);
    mel_render_scene_destroy(scene);
    ecs_fini(world);
}

MEL_TEST(source_ecs_2d_sync_removed, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(.world = world, .alloc = mel_alloc_heap());
    Mel_Render_Scene* scene = make_scene(dev);
    mel_render_scene_attach_source(scene, source);

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

    mel_render_scene_sync(scene);
    Mel_Render_Manager* mgr = mel_render_scene_manager(scene);
    MEL_ASSERT_EQ(mel_mgr_count(mgr), 2);

    ecs_delete(world, e1);

    mel_render_scene_sync(scene);
    MEL_ASSERT_EQ(mel_mgr_count(mgr), 1);

    Mel_Render_Handle h1 = mel_source_ecs_2d_handle_for_entity(source, e1);
    MEL_ASSERT(!mel_render_handle_valid(h1));

    Mel_Render_Handle h2 = mel_source_ecs_2d_handle_for_entity(source, e2);
    MEL_ASSERT(mel_render_handle_valid(h2));

    mel_render_source_destroy(source);
    mel_render_scene_destroy(scene);
    ecs_fini(world);
}

MEL_TEST(source_ecs_2d_handle_lookup, .tags = "render, visual")
{
    Mel_Gpu_Device* dev = test_gpu_dev();
    if (!dev) { MEL_FAIL("no gpu device"); return; }

    ecs_world_t* world = ecs_init();
    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    Mel_Render_Source* source = mel_source_ecs_2d_create(.world = world, .alloc = mel_alloc_heap());
    Mel_Render_Scene* scene = make_scene(dev);
    mel_render_scene_attach_source(scene, source);

    Mel_Render_Handle before = mel_source_ecs_2d_handle_for_entity(source, 9999);
    MEL_ASSERT(!mel_render_handle_valid(before));

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Mel_CTransform, { .pos = mel_vec2(5.0f, 5.0f) });
    ecs_set(world, e, Mel_Sprite, {
        .size = mel_vec2(8.0f, 8.0f),
        .color = mel_vec4(1, 1, 1, 1),
        .uv = mel_rect(0, 0, 1, 1),
    });

    mel_render_scene_sync(scene);

    Mel_Render_Handle after = mel_source_ecs_2d_handle_for_entity(source, e);
    MEL_ASSERT(mel_render_handle_valid(after));

    ecs_delete(world, e);
    mel_render_scene_sync(scene);

    Mel_Render_Handle gone = mel_source_ecs_2d_handle_for_entity(source, e);
    MEL_ASSERT(!mel_render_handle_valid(gone));

    mel_render_source_destroy(source);
    mel_render_scene_destroy(scene);
    ecs_fini(world);
}
