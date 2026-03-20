#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.device.h"
#include "gpu.swapchain.h"
#include "render.viewport.h"
#include "render.target.h"
#include "render.scene.h"
#include "render.source.ecs.2d.h"
#include "render.pipeline.scene_forward.h"
#include "render.types.2d.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <flecs.h>

#define WIN_W 800
#define WIN_H 600

static Mel_Window_Handle     s_window;
static Mel_Swapchain_Handle  s_swapchain;
static Mel_Render_Target_Handle s_target;
static Mel_Render_Scene*     s_scene;
static Mel_Render_Source*    s_source;
static Mel_Render_View_Handle s_view;
static ecs_world_t*          s_world;

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    s_window = mel_window_create(S8("Sprite 2D"), .width = WIN_W, .height = WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(dev, s_window);
    s_target = mel_render_target_from_swapchain(s_swapchain);

    s_world = ecs_init();
    mel_component_transform_register(s_world);
    mel_component_sprite_register(s_world);

    s_source = mel_source_ecs_2d_create(.world = s_world, .alloc = alloc);
    s_scene = mel_render_scene_create(
        .dev = dev,
        .alloc = alloc);
    mel_render_scene_attach_source(s_scene, s_source);

    Mel_Render_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)WIN_W, (f32)WIN_H, 0, -1, 1),
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    };

    s_view = mel_render_view_create(
        .scene    = s_scene,
        .camera   = camera,
        .target   = s_target,
        .pipeline = S8("scene_forward"),
        .dev      = dev,
        .alloc    = alloc);

    ecs_entity_t e0 = ecs_new(s_world);
    ecs_set(s_world, e0, Mel_CTransform, { .pos = mel_vec2(200, 150) });
    ecs_set(s_world, e0, Mel_Sprite, {
        .size  = mel_vec2(120, 80),
        .color = {{ 0.9f, 0.2f, 0.3f, 1.0f }},
        .uv    = mel_rect(0, 0, 1, 1),
    });

    ecs_entity_t e1 = ecs_new(s_world);
    ecs_set(s_world, e1, Mel_CTransform, { .pos = mel_vec2(500, 300) });
    ecs_set(s_world, e1, Mel_Sprite, {
        .size  = mel_vec2(60, 60),
        .color = {{ 0.2f, 0.8f, 0.4f, 1.0f }},
        .uv    = mel_rect(0, 0, 1, 1),
    });

    ecs_entity_t e2 = ecs_new(s_world);
    ecs_set(s_world, e2, Mel_CTransform, { .pos = mel_vec2(350, 450) });
    ecs_set(s_world, e2, Mel_Sprite, {
        .size  = mel_vec2(200, 40),
        .color = {{ 0.3f, 0.4f, 0.9f, 1.0f }},
        .uv    = mel_rect(0, 0, 1, 1),
    });

    ecs_entity_t e3 = ecs_new(s_world);
    ecs_set(s_world, e3, Mel_CTransform, { .pos = mel_vec2(650, 100) });
    ecs_set(s_world, e3, Mel_Sprite, {
        .size  = mel_vec2(80, 120),
        .color = {{ 1.0f, 0.8f, 0.1f, 1.0f }},
        .uv    = mel_rect(0, 0, 1, 1),
    });
}

void app_shutdown(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_device_wait_idle(dev);

    mel_render_view_destroy(s_view);
    mel_render_source_destroy(s_source);
    mel_render_scene_destroy(s_scene);
    mel_render_target_destroy(s_target);
    ecs_fini(s_world);
}
