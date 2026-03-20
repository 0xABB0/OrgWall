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
#include "ecs.2d.text.h"
#include "font.desc.h"
#include "font.atlas.h"
#include "font.sdf.h"
#include "font.msdf.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "vfs.h"
#include "vfs.backend.os.h"

#include <flecs.h>

#define WIN_W 1024
#define WIN_H 768

static Mel_Window_Handle s_window;
static Mel_Swapchain_Handle s_swapchain;
static Mel_Render_Target_Handle s_target;
static Mel_Render_Scene* s_scene;
static Mel_Render_Source* s_source;
static Mel_Render_View_Handle s_view;
static ecs_world_t* s_world;

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    s_window = mel_window_create(S8("Text Techniques"), .width = WIN_W, .height = WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(dev, s_window);
    s_target = mel_render_target_from_swapchain(s_swapchain);

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    s_world = ecs_init();
    mel_component_transform_register(s_world);
    mel_component_sprite_register(s_world);
    mel_component_text_register(s_world);

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

    Mel_Font_Desc_Handle monaco = mel_font_desc_load_ttf(S8("/System/Library/Fonts/Monaco.ttf"));

    Mel_Font_Atlas_Handle atlas_font = mel_font_atlas_load(.desc = monaco, .size = 28.0f);
    Mel_Font_SDF_Handle sdf_font = mel_font_sdf_load(.desc = monaco, .size = 40.0f);
    Mel_Font_MSDF_Handle msdf_font = mel_font_msdf_load(
        .desc = monaco, .size = 128.0f,
        .atlas_width = 2048, .atlas_height = 2048,
        .px_range = 12.0f, .padding = 18);

    Mel_Vec4 warm = {{ 0.96f, 0.91f, 0.79f, 1.0f }};
    Mel_Vec4 green = {{ 0.82f, 0.96f, 0.87f, 1.0f }};
    Mel_Vec4 blue = {{ 0.86f, 0.90f, 0.99f, 1.0f }};

    f32 panel_x = 44.0f;
    f32 text_x = panel_x + 28.0f;

    ecs_entity_t panel1 = ecs_new(s_world);
    ecs_set(s_world, panel1, Mel_CTransform, { .pos = mel_vec2(panel_x + 300, 52 + 120) });
    ecs_set(s_world, panel1, Mel_Sprite, {
        .size = mel_vec2(600, 240),
        .color = {{ 0.20f, 0.15f, 0.11f, 1.0f }},
        .uv = mel_rect(0, 0, 1, 1),
    });

    Mel_CText ct1 = mel_ctext_atlas(atlas_font, S8("Bitmap Atlas"), warm);
    ecs_entity_t t1 = ecs_new(s_world);
    ecs_set(s_world, t1, Mel_CTransform, { .pos = mel_vec2(text_x, 60) });
    ecs_set_id(s_world, t1, ecs_id(Mel_CText), sizeof(Mel_CText), &ct1);

    Mel_CText ct2 = mel_ctext_atlas(atlas_font,
        S8("Fast, crisp at native size.\nScaling exposes the pixels."), warm);
    ecs_entity_t t2 = ecs_new(s_world);
    ecs_set(s_world, t2, Mel_CTransform, { .pos = mel_vec2(text_x, 100) });
    ecs_set_id(s_world, t2, ecs_id(Mel_CText), sizeof(Mel_CText), &ct2);

    ecs_entity_t panel2 = ecs_new(s_world);
    ecs_set(s_world, panel2, Mel_CTransform, { .pos = mel_vec2(panel_x + 300, 280 + 110) });
    ecs_set(s_world, panel2, Mel_Sprite, {
        .size = mel_vec2(600, 220),
        .color = {{ 0.10f, 0.18f, 0.13f, 1.0f }},
        .uv = mel_rect(0, 0, 1, 1),
    });

    Mel_CText ct3 = mel_ctext_sdf(sdf_font, S8("Signed Distance Field"), green);
    ecs_entity_t t3 = ecs_new(s_world);
    ecs_set(s_world, t3, Mel_CTransform, { .pos = mel_vec2(text_x, 260) });
    ecs_set_id(s_world, t3, ecs_id(Mel_CText), sizeof(Mel_CText), &ct3);

    Mel_CText ct4 = mel_ctext_sdf(sdf_font,
        S8("Smooth scaling and cheap effects.\nCorners soften at extremes."), green);
    ecs_entity_t t4 = ecs_new(s_world);
    ecs_set(s_world, t4, Mel_CTransform, { .pos = mel_vec2(text_x, 310) });
    ecs_set_id(s_world, t4, ecs_id(Mel_CText), sizeof(Mel_CText), &ct4);

    ecs_entity_t panel3 = ecs_new(s_world);
    ecs_set(s_world, panel3, Mel_CTransform, { .pos = mel_vec2(panel_x + 300, 520 + 80) });
    ecs_set(s_world, panel3, Mel_Sprite, {
        .size = mel_vec2(600, 160),
        .color = {{ 0.11f, 0.13f, 0.21f, 1.0f }},
        .uv = mel_rect(0, 0, 1, 1),
    });

    Mel_CText ct5 = mel_ctext_msdf(msdf_font, S8("Multi-Channel SDF"), blue);
    ct5.scale = 0.20f;
    ecs_entity_t t5 = ecs_new(s_world);
    ecs_set(s_world, t5, Mel_CTransform, { .pos = mel_vec2(text_x, 500) });
    ecs_set_id(s_world, t5, ecs_id(Mel_CText), sizeof(Mel_CText), &ct5);

    Mel_CText ct6 = mel_ctext_msdf(msdf_font,
        S8("Sharper corners, cleaner outlines.\nBest for scale headroom."), blue);
    ct6.scale = 0.20f;
    ecs_entity_t t6 = ecs_new(s_world);
    ecs_set(s_world, t6, Mel_CTransform, { .pos = mel_vec2(text_x, 550) });
    ecs_set_id(s_world, t6, ecs_id(Mel_CText), sizeof(Mel_CText), &ct6);

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

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        mel_quit();
}
