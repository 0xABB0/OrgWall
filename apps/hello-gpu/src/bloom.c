#include <stdlib.h>

#include <log/log.h>

#include "bloom.h"
#include "hud.h"

static const char BLOOM_SLANG[] = {
#embed "shaders/slang/bloom.slang"
    , 0
};

typedef struct
{
    u32   tex0;
    u32   tex1;
    u32   smp;
    u32   img;
    u32   w;
    u32   h;
    float param0;
    float param1;
} Bloom_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    bool             img_fresh;

    Mel_Gpu_Shader   scene_sh;
    Mel_Gpu_Pipeline scene_pl;
    Mel_Gpu_Shader   bright_sh;
    Mel_Gpu_Pipeline bright_pl;
    Mel_Gpu_Shader   blurx_sh;
    Mel_Gpu_Pipeline blurx_pl;
    Mel_Gpu_Shader   blury_sh;
    Mel_Gpu_Pipeline blury_pl;
    Mel_Gpu_Shader   composite_sh;
    Mel_Gpu_Pipeline composite_pl;
    Mel_Gpu_Sampler  sampler;

    i32                  w, h;
    Mel_Gpu_Texture      img_scene;
    Mel_Gpu_Texture_View img_scene_view;
    u32                  img_scene_slot;
    Mel_Gpu_Texture      img_bright;
    Mel_Gpu_Texture_View img_bright_view;
    u32                  img_bright_slot;
    Mel_Gpu_Texture      img_blurx;
    Mel_Gpu_Texture_View img_blurx_view;
    u32                  img_blurx_slot;
    Mel_Gpu_Texture      img_bloom;
    Mel_Gpu_Texture_View img_bloom_view;
    u32                  img_bloom_slot;

    Hud hud;
    f64 t;
} Bloom;

static inline bool handle_zero(Mel_SlotMap_Handle h) { return h.index == 0 && h.generation == 0; }

static void destroy_imgs(Bloom* b)
{
    if (!handle_zero(b->img_scene_view.slot)) mel_gpu_texture_view_destroy(b->dev, b->img_scene_view);
    if (!handle_zero(b->img_scene.slot))      mel_gpu_texture_destroy(b->dev, b->img_scene);
    if (!handle_zero(b->img_bright_view.slot)) mel_gpu_texture_view_destroy(b->dev, b->img_bright_view);
    if (!handle_zero(b->img_bright.slot))      mel_gpu_texture_destroy(b->dev, b->img_bright);
    if (!handle_zero(b->img_blurx_view.slot)) mel_gpu_texture_view_destroy(b->dev, b->img_blurx_view);
    if (!handle_zero(b->img_blurx.slot))      mel_gpu_texture_destroy(b->dev, b->img_blurx);
    if (!handle_zero(b->img_bloom_view.slot)) mel_gpu_texture_view_destroy(b->dev, b->img_bloom_view);
    if (!handle_zero(b->img_bloom.slot))      mel_gpu_texture_destroy(b->dev, b->img_bloom);
    b->img_scene_view  = b->img_bright_view = b->img_blurx_view = b->img_bloom_view = (Mel_Gpu_Texture_View){ 0 };
    b->img_scene       = b->img_bright       = b->img_blurx       = b->img_bloom       = (Mel_Gpu_Texture){ 0 };
}

static void make_imgs(Bloom* b, i32 w, i32 h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == b->w && h == b->h && !handle_zero(b->img_scene.slot)) return;
    destroy_imgs(b);
    b->w = w; b->h = h;

    b->img_scene  = mel_gpu_texture_create(b->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = "bloom-scene").value;
    b->img_bright = mel_gpu_texture_create(b->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = "bloom-bright").value;
    b->img_blurx  = mel_gpu_texture_create(b->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = "bloom-blurx").value;
    b->img_bloom  = mel_gpu_texture_create(b->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = "bloom-blur").value;

    b->img_scene_view  = mel_gpu_texture_default_view(b->dev, b->img_scene).value;
    b->img_bright_view = mel_gpu_texture_default_view(b->dev, b->img_bright).value;
    b->img_blurx_view  = mel_gpu_texture_default_view(b->dev, b->img_blurx).value;
    b->img_bloom_view  = mel_gpu_texture_default_view(b->dev, b->img_bloom).value;

    b->img_scene_slot  = mel_gpu_texture_view_bindless_slot(b->dev, b->img_scene_view);
    b->img_bright_slot = mel_gpu_texture_view_bindless_slot(b->dev, b->img_bright_view);
    b->img_blurx_slot  = mel_gpu_texture_view_bindless_slot(b->dev, b->img_blurx_view);
    b->img_bloom_slot  = mel_gpu_texture_view_bindless_slot(b->dev, b->img_bloom_view);

    b->img_fresh = true;
}

static void* bloom_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Bloom* b = calloc(1, sizeof *b);
    b->dev = dev;
    hud_init(&b->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "bloom: bindless heap unavailable");
        return b;
    }

    Mel_Gpu_Pipeline_From_Slang_Result scene = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BLOOM_SLANG, .compute_entry = "cs_scene", .push_constant_size = sizeof(Bloom_Root), .bindless = true, .name = "bloom-scene");
    if (mel_gpu_failed(scene.status)) return b;
    b->scene_sh = scene.shader;
    b->scene_pl = scene.value;

    Mel_Gpu_Pipeline_From_Slang_Result bright = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BLOOM_SLANG, .compute_entry = "cs_bright", .push_constant_size = sizeof(Bloom_Root), .bindless = true, .name = "bloom-bright");
    if (mel_gpu_failed(bright.status)) return b;
    b->bright_sh = bright.shader;
    b->bright_pl = bright.value;

    Mel_Gpu_Pipeline_From_Slang_Result blurx = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BLOOM_SLANG, .compute_entry = "cs_blurx", .push_constant_size = sizeof(Bloom_Root), .bindless = true, .name = "bloom-blurx");
    if (mel_gpu_failed(blurx.status)) return b;
    b->blurx_sh = blurx.shader;
    b->blurx_pl = blurx.value;

    Mel_Gpu_Pipeline_From_Slang_Result blury = mel_gpu_pipeline_compute_create_from_slang(dev, .source = BLOOM_SLANG, .compute_entry = "cs_blury", .push_constant_size = sizeof(Bloom_Root), .bindless = true, .name = "bloom-blury");
    if (mel_gpu_failed(blury.status)) return b;
    b->blury_sh = blury.shader;
    b->blury_pl = blury.value;

    Mel_Gpu_Pipeline_From_Slang_Result composite = mel_gpu_pipeline_create_from_slang(dev,
                                                                                      .source = BLOOM_SLANG,
                                                                                      .vertex_entry = "vs_composite",
                                                                                      .fragment_entry = "fs_composite",
                                                                                      .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                      .cull = MEL_GPU_CULL_NONE,
                                                                                      .color_format = mel_gpu_swapchain_format(sc),
                                                                                      .bindless = true,
                                                                                      .name = "bloom-composite");
    if (mel_gpu_failed(composite.status)) return b;
    b->composite_sh = composite.shader;
    b->composite_pl = composite.value;

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "bloom-sampler");
    if (mel_gpu_failed(smp.status)) return b;
    b->sampler = smp.value;

    Mel_Gpu_Swapchain_Extent ext = mel_gpu_swapchain_extent(sc);
    make_imgs(b, (i32)ext.width, (i32)ext.height);

    b->ready = true;
    return b;
}

static void bloom_resize(void* state, i32 w, i32 h)
{
    Bloom* b = state;
    if (!b->ready) return;
    make_imgs(b, w, h);
}

static void barrier_img(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    Mel_Gpu_Subresource_Range r = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tex, r, src, dst);
}

static void bloom_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Bloom* b = state;
    b->t += dt;
    hud_frame(&b->hud, dt, "HDR bloom · scene compute → bright-pass → separable blur → Reinhard tonemap");

    if (!b->ready || handle_zero(b->img_scene.slot))
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.02f, 0.02f, 0.05f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    bool fresh = b->img_fresh;
    b->img_fresh = false;
    Mel_Gpu_Resource_State init_src = fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;

    u32 gx = ((u32)b->w + 7) / 8;
    u32 gy = ((u32)b->h + 7) / 8;
    u32 smp_slot = mel_gpu_sampler_bindless_slot(b->dev, b->sampler);

    barrier_img(cmd, b->img_scene,  init_src, MEL_GPU_STATE_UNORDERED_ACCESS);
    Bloom_Root sr = { .tex0 = b->img_scene_slot, .tex1 = b->img_scene_slot, .smp = smp_slot, .img = b->img_scene_slot, .w = (u32)b->w, .h = (u32)b->h, .param0 = (f32)b->t };
    mel_gpu_cmd_bind_pipeline(cmd, b->scene_pl);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof sr, &sr);
    mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
    barrier_img(cmd, b->img_scene, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    barrier_img(cmd, b->img_bright, init_src, MEL_GPU_STATE_UNORDERED_ACCESS);
    Bloom_Root br = { .tex0 = b->img_scene_slot, .tex1 = b->img_scene_slot, .smp = smp_slot, .img = b->img_bright_slot, .w = (u32)b->w, .h = (u32)b->h, .param0 = 0.55f };
    mel_gpu_cmd_bind_pipeline(cmd, b->bright_pl);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof br, &br);
    mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
    barrier_img(cmd, b->img_bright, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    barrier_img(cmd, b->img_blurx, init_src, MEL_GPU_STATE_UNORDERED_ACCESS);
    Bloom_Root bxr = { .tex0 = b->img_bright_slot, .tex1 = b->img_bright_slot, .smp = smp_slot, .img = b->img_blurx_slot, .w = (u32)b->w, .h = (u32)b->h };
    mel_gpu_cmd_bind_pipeline(cmd, b->blurx_pl);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof bxr, &bxr);
    mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
    barrier_img(cmd, b->img_blurx, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    barrier_img(cmd, b->img_bloom, init_src, MEL_GPU_STATE_UNORDERED_ACCESS);
    Bloom_Root byr = { .tex0 = b->img_blurx_slot, .tex1 = b->img_blurx_slot, .smp = smp_slot, .img = b->img_bloom_slot, .w = (u32)b->w, .h = (u32)b->h };
    mel_gpu_cmd_bind_pipeline(cmd, b->blury_pl);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof byr, &byr);
    mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
    barrier_img(cmd, b->img_bloom, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    Bloom_Root cr = { .tex0 = b->img_scene_slot, .tex1 = b->img_bloom_slot, .smp = smp_slot, .img = b->img_scene_slot, .w = (u32)b->w, .h = (u32)b->h, .param0 = 1.8f };
    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, b->composite_pl);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof cr, &cr);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_pass(cmd);
}

static void bloom_teardown(void* state)
{
    Bloom* b = state;
    if (!b) return;
    if (b->ready)
    {
        destroy_imgs(b);
        mel_gpu_sampler_destroy(b->dev, b->sampler);
        mel_gpu_pipeline_destroy(b->dev, b->composite_pl);
        mel_gpu_shader_destroy(b->dev, b->composite_sh);
        mel_gpu_pipeline_destroy(b->dev, b->blury_pl);
        mel_gpu_shader_destroy(b->dev, b->blury_sh);
        mel_gpu_pipeline_destroy(b->dev, b->blurx_pl);
        mel_gpu_shader_destroy(b->dev, b->blurx_sh);
        mel_gpu_pipeline_destroy(b->dev, b->bright_pl);
        mel_gpu_shader_destroy(b->dev, b->bright_sh);
        mel_gpu_pipeline_destroy(b->dev, b->scene_pl);
        mel_gpu_shader_destroy(b->dev, b->scene_sh);
    }
    free(b);
}

const Graphical_App BLOOM_APP = {
    .title    = "hdr-bloom",
    .init     = bloom_init,
    .resize   = bloom_resize,
    .render   = bloom_render,
    .teardown = bloom_teardown,
};
