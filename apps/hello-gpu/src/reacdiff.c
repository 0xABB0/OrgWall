#include <stdlib.h>

#include <log/log.h>

#include "reacdiff.h"
#include "hud.h"

static const char REACDIFF_SLANG[] = {
#embed "shaders/slang/reacdiff.slang"
    , 0
};

#define STEPS_PER_FRAME 8

typedef struct
{
    u32 tex;
    u32 smp;
    u32 img;
    u32 w;
    u32 h;
    f32 da;
    f32 db;
    f32 feed;
    f32 kill;
    f32 dt;
    f32 time;
} Reacdiff_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    bool             initialized;

    Mel_Gpu_Shader   init_sh;
    Mel_Gpu_Pipeline init_pl;
    Mel_Gpu_Shader   step_sh;
    Mel_Gpu_Pipeline step_pl;
    Mel_Gpu_Shader   draw_sh;
    Mel_Gpu_Pipeline draw_pl;
    Mel_Gpu_Sampler  sampler;
    u32              smp_slot;

    i32                  w, h;
    Mel_Gpu_Texture      img[2];
    Mel_Gpu_Texture_View img_view[2];
    u32                  img_slot[2];
    bool                 img_fresh;
    i32                  cur;

    Hud hud;
    f64 t;
} Reacdiff;

static inline bool handle_zero(Mel_SlotMap_Handle h) { return h.index == 0 && h.generation == 0; }

static void destroy_imgs(Reacdiff* r)
{
    for (i32 k = 0; k < 2; ++k)
    {
        if (!handle_zero(r->img_view[k].slot)) mel_gpu_texture_view_destroy(r->dev, r->img_view[k]);
        if (!handle_zero(r->img[k].slot))      mel_gpu_texture_destroy(r->dev, r->img[k]);
        r->img_view[k] = (Mel_Gpu_Texture_View){ 0 };
        r->img[k]      = (Mel_Gpu_Texture){ 0 };
    }
}

static void make_imgs(Reacdiff* r, i32 w, i32 h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == r->w && h == r->h && !handle_zero(r->img[0].slot)) return;
    destroy_imgs(r);
    r->w = w; r->h = h;
    for (i32 k = 0; k < 2; ++k)
    {
        r->img[k]      = mel_gpu_texture_create(r->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = "reacdiff").value;
        r->img_view[k] = mel_gpu_texture_default_view(r->dev, r->img[k]).value;
        r->img_slot[k] = mel_gpu_texture_view_bindless_slot(r->dev, r->img_view[k]);
    }
    r->img_fresh  = true;
    r->initialized = false;
    r->cur = 0;
}

static void* reacdiff_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Reacdiff* r = calloc(1, sizeof *r);
    r->dev = dev;
    hud_init(&r->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "reacdiff: bindless heap unavailable");
        return r;
    }

    Mel_Gpu_Pipeline_From_Slang_Result init = mel_gpu_pipeline_compute_create_from_slang(dev, .source = REACDIFF_SLANG, .compute_entry = "cs_init", .push_constant_size = sizeof(Reacdiff_Root), .bindless = true, .name = "reacdiff-init");
    if (mel_gpu_failed(init.status))
        return r;
    r->init_sh = init.shader;
    r->init_pl = init.value;

    Mel_Gpu_Pipeline_From_Slang_Result step = mel_gpu_pipeline_compute_create_from_slang(dev, .source = REACDIFF_SLANG, .compute_entry = "cs_step", .push_constant_size = sizeof(Reacdiff_Root), .bindless = true, .name = "reacdiff-step");
    if (mel_gpu_failed(step.status))
        return r;
    r->step_sh = step.shader;
    r->step_pl = step.value;

    Mel_Gpu_Pipeline_From_Slang_Result draw = mel_gpu_pipeline_create_from_slang(dev,
                                                                                 .source = REACDIFF_SLANG,
                                                                                 .vertex_entry = "vs_draw",
                                                                                 .fragment_entry = "fs_draw",
                                                                                 .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                 .cull = MEL_GPU_CULL_NONE,
                                                                                 .color_format = mel_gpu_swapchain_format(sc),
                                                                                 .bindless = true,
                                                                                 .name = "reacdiff-draw");
    if (mel_gpu_failed(draw.status))
        return r;
    r->draw_sh = draw.shader;
    r->draw_pl = draw.value;

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_REPEAT, .wrap_v = MEL_GPU_WRAP_REPEAT, .name = "reacdiff-sampler");
    if (mel_gpu_failed(smp.status)) return r;
    r->sampler = smp.value;
    r->smp_slot = mel_gpu_sampler_bindless_slot(dev, r->sampler);

    Mel_Gpu_Swapchain_Extent ext = mel_gpu_swapchain_extent(sc);
    make_imgs(r, (i32)ext.width, (i32)ext.height);

    r->ready = true;
    return r;
}

static void reacdiff_resize(void* state, i32 w, i32 h)
{
    Reacdiff* r = state;
    if (!r->ready) return;
    make_imgs(r, w, h);
}

static void barrier_img(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    mel_gpu_cmd_texture_barrier(cmd, tex, range, src, dst);
}

static void reacdiff_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Reacdiff* r = state;
    r->t += dt;
    hud_frame(&r->hud, dt, "reaction-diffusion · Gray-Scott · compute ping-pong → colormap");

    if (!r->ready || handle_zero(r->img[0].slot))
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.0f, 0.0f, 0.05f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    bool fresh = r->img_fresh;
    r->img_fresh = false;
    u32  gx = ((u32)r->w + 7) / 8;
    u32  gy = ((u32)r->h + 7) / 8;

    if (!r->initialized)
    {
        r->initialized = true;
        for (i32 k = 0; k < 2; ++k)
        {
            Mel_Gpu_Resource_State is = fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_UNORDERED_ACCESS;
            barrier_img(cmd, r->img[k], is, MEL_GPU_STATE_UNORDERED_ACCESS);
            Reacdiff_Root ir = { .img = r->img_slot[k], .w = (u32)r->w, .h = (u32)r->h };
            mel_gpu_cmd_bind_pipeline(cmd, r->init_pl);
            mel_gpu_cmd_bind_bindless(cmd);
            mel_gpu_cmd_push_constants(cmd, 0, sizeof ir, &ir);
            mel_gpu_cmd_dispatch(cmd, gx, gy, 1);
            barrier_img(cmd, r->img[k], MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);
        }
        fresh = false;
    }

    for (i32 s = 0; s < STEPS_PER_FRAME; ++s)
    {
        i32 src = r->cur;
        i32 dst = r->cur ^ 1;

        Mel_Gpu_Resource_State src_state = (s == 0 && fresh) ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
        Mel_Gpu_Resource_State dst_state = (s == 0 && fresh) ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;

        barrier_img(cmd, r->img[src], src_state, MEL_GPU_STATE_SHADER_RESOURCE);
        barrier_img(cmd, r->img[dst], dst_state, MEL_GPU_STATE_UNORDERED_ACCESS);

        Reacdiff_Root sr = {
            .tex = r->img_slot[src],
            .smp = r->smp_slot,
            .img = r->img_slot[dst],
            .w = (u32)r->w, .h = (u32)r->h,
            .da = 1.0f, .db = 0.5f,
            .feed = 0.055f, .kill = 0.062f,
            .dt = 1.0f,
        };
        mel_gpu_cmd_bind_pipeline(cmd, r->step_pl);
        mel_gpu_cmd_bind_bindless(cmd);
        mel_gpu_cmd_push_constants(cmd, 0, sizeof sr, &sr);
        mel_gpu_cmd_dispatch(cmd, gx, gy, 1);

        barrier_img(cmd, r->img[dst], MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);
        r->cur = dst;
    }

    Reacdiff_Root dr = { .tex = r->img_slot[r->cur], .smp = r->smp_slot, .time = (f32)r->t };
    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.0f, 0.0f, 0.0f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, r->draw_pl);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof dr, &dr);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_pass(cmd);
}

static void reacdiff_teardown(void* state)
{
    Reacdiff* r = state;
    if (!r) return;
    if (r->ready)
    {
        destroy_imgs(r);
        mel_gpu_sampler_destroy(r->dev, r->sampler);
        mel_gpu_pipeline_destroy(r->dev, r->draw_pl);
        mel_gpu_shader_destroy(r->dev, r->draw_sh);
        mel_gpu_pipeline_destroy(r->dev, r->step_pl);
        mel_gpu_shader_destroy(r->dev, r->step_sh);
        mel_gpu_pipeline_destroy(r->dev, r->init_pl);
        mel_gpu_shader_destroy(r->dev, r->init_sh);
    }
    free(r);
}

const Graphical_App REACDIFF_APP = {
    .title    = "reaction-diffusion",
    .init     = reacdiff_init,
    .resize   = reacdiff_resize,
    .render   = reacdiff_render,
    .teardown = reacdiff_teardown,
};
