#include "../melody/core.app.h"
#include "../melody/core.engine.h"
#include "../melody/window.h"
#include "../melody/swapchain.h"
#include "../melody/gpu.device.h"
#include "../melody/gpu.swapchain.h"
#include "../melody/gpu.texture.h"
#include "../melody/render.viewport.h"
#include "../melody/render.target.h"
#include "../melody/render.texture_table.h"
#include "../melody/render.source.ecs.2d.h"
#include "../melody/render.pipeline.2d.h"
#include "../melody/ecs.2d.transform.h"
#include "../melody/ecs.2d.sprite.h"
#include "../melody/math.mat4.h"
#include "../melody/math.vec2.h"
#include "../melody/math.vec4.h"
#include "../melody/math.geo.rect.h"
#include "../melody/allocator.heap.h"
#include "../melody/string.str8.h"

#include <flecs.h>

#define WIN_W 800
#define WIN_H 600

static Mel_Window_Handle     s_window;
static Mel_Swapchain_Handle  s_swapchain;
static Mel_Render_Target*    s_target;
static Mel_Render_Source*    s_source;
static Mel_Render_View*      s_view;
static Mel_Texture_Table     s_texture_table;
static Mel_Gpu_Texture       s_white_tex;
static ecs_world_t*          s_world;

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    s_window = mel_window_create(S8("Sprite 2D"), .width = WIN_W, .height = WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(dev, s_window);
    s_target = mel_render_target_from_swapchain(s_swapchain, alloc);

    mel_texture_table_init(&s_texture_table, dev, alloc, .capacity = 64);

    mel_gpu_texture_init_white(&s_white_tex, dev);
    mel_texture_table_add(&s_texture_table, s_white_tex.image._view, s_white_tex._sampler);

    mel_pipeline_2d_set_texture_table(&s_texture_table);

    s_world = ecs_init();
    mel_component_transform_register(s_world);
    mel_component_sprite_register(s_world);

    s_source = mel_source_ecs_2d_create(.world = s_world, .alloc = alloc);

    Mel_Render_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)WIN_W, (f32)WIN_H, 0, -1, 1),
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    };

    s_view = mel_render_view_create(
        .source   = s_source,
        .camera   = camera,
        .target   = s_target,
        .pipeline = S8("default_2d"),
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
    mel_render_target_destroy(s_target);
    mel_texture_table_shutdown(&s_texture_table);
    mel_gpu_texture_shutdown(&s_white_tex, dev);
    ecs_fini(s_world);
}
