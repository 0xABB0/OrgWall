#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "gpu.device.h"
#include "gpu.texture.h"
#include "render.viewport.h"
#include "render.target.h"
#include "render.texture_table.h"
#include "render.source.ecs.2d.h"
#include "render.pipeline.2d.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <flecs.h>

#define DESIGN_W 320
#define DESIGN_H 240
#define WIN_W    960
#define WIN_H    720

static Mel_Window_Handle s_window;
static Mel_Swapchain_Handle s_swapchain;
static Mel_Render_Target_Handle s_target;
static Mel_Render_Source* s_source;
static Mel_Render_View* s_view;
static Mel_Texture_Table s_tex_table;
static Mel_Gpu_Texture s_white_tex;
static ecs_world_t* s_world;

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    s_window = mel_window_create(S8("Resolution Independence"),
        .width = WIN_W, .height = WIN_H, .flags = SDL_WINDOW_RESIZABLE);
    s_swapchain = mel_gpu_swapchain_create_for_window(dev, s_window);
    s_target = mel_render_target_from_swapchain(s_swapchain);

    mel_texture_table_init(&s_tex_table, dev, alloc, .capacity = 64);
    mel_gpu_texture_init_white(&s_white_tex, dev);
    mel_texture_table_add(&s_tex_table, s_white_tex.image._view, s_white_tex._sampler);
    mel_pipeline_2d_set_texture_table(&s_tex_table);

    s_world = ecs_init();
    mel_component_transform_register(s_world);
    mel_component_sprite_register(s_world);

    s_source = mel_source_ecs_2d_create(.world = s_world, .alloc = alloc);

    for (i32 y = 0; y < 6; y++)
    {
        for (i32 x = 0; x < 8; x++)
        {
            ecs_entity_t e = ecs_new(s_world);
            f32 px = 20.0f + (f32)x * 38.0f;
            f32 py = 20.0f + (f32)y * 38.0f;
            f32 r = (f32)x / 7.0f;
            f32 g = (f32)y / 5.0f;
            f32 b = 1.0f - r;
            ecs_set(s_world, e, Mel_CTransform, { .pos = mel_vec2(px, py) });
            ecs_set(s_world, e, Mel_Sprite, {
                .size = mel_vec2(32, 32),
                .color = {{ r, g, b, 1 }},
                .uv = mel_rect(0, 0, 1, 1),
            });
        }
    }

    Mel_Render_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)DESIGN_W, (f32)DESIGN_H, 0, -1, 1),
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    };

    s_view = mel_render_view_create(
        .source = s_source,
        .camera = camera,
        .target = s_target,
        .pipeline = S8("default_2d"),
        .dev = dev,
        .alloc = alloc,
        .design_width = DESIGN_W,
        .design_height = DESIGN_H,
        .scale_mode = MEL_SCALE_FIT);
}

void app_shutdown(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_device_wait_idle(dev);

    mel_render_view_destroy(s_view);
    mel_render_source_destroy(s_source);
    mel_render_target_destroy(s_target);
    mel_texture_table_shutdown(&s_tex_table);
    mel_gpu_texture_shutdown(&s_white_tex, dev);
    ecs_fini(s_world);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        mel_quit();
}
