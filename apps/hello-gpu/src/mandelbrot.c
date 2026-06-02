#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "mandelbrot.h"
#include "hud.h"
#include "bindless_present.h"
#include "mandelbrot_spv.h"

#define MAX_ITER 512

typedef struct
{
    u32 image, w, h, max_iter;
    f32 center_x, center_y, scale, time;
} Mandel_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    Mel_Gpu_Shader   shader;
    Mel_Gpu_Pipeline pipeline;

    i32                  w, h;
    Mel_Gpu_Texture      img;
    Mel_Gpu_Texture_View img_view;
    u32                  img_slot;
    bool                 img_fresh;

    Bindless_Present present;
    Hud              hud;
    f64              t;
} Mandel;

static inline bool handle_zero(Mel_SlotMap_Handle h) { return h.index == 0 && h.generation == 0; }

static void destroy_img(Mandel* m)
{
    if (!handle_zero(m->img_view.slot))
        mel_gpu_texture_view_destroy(m->dev, m->img_view);
    if (!handle_zero(m->img.slot))
        mel_gpu_texture_destroy(m->dev, m->img);
    m->img_view = (Mel_Gpu_Texture_View){ 0 };
    m->img = (Mel_Gpu_Texture){ 0 };
}

static void make_img(Mandel* m, i32 w, i32 h)
{
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (w == m->w && h == m->h && !handle_zero(m->img.slot))
        return;
    destroy_img(m);
    m->w = w;
    m->h = h;
    m->img = mel_gpu_texture_create(m->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)w, (u32)h, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_STORAGE | MEL_GPU_TEXTURE_SAMPLED, .name = "mandel-canvas").value;
    m->img_view = mel_gpu_texture_default_view(m->dev, m->img).value;
    m->img_slot = mel_gpu_texture_view_bindless_slot(m->dev, m->img_view);
    m->img_fresh = true;
}

static void* mandel_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Mandel* m = calloc(1, sizeof *m);
    m->dev = dev;
    hud_init(&m->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "mandelbrot: bindless heap unavailable; storage-image class needs it");
        return m;
    }

    Mel_Gpu_Shader_Create_Result cs = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = MANDELBROT_COMP_SPV, .spirv_size = sizeof MANDELBROT_COMP_SPV, .entry = "main", .name = "mandelbrot");
    if (mel_gpu_failed(cs.status))
        return m;
    m->shader = cs.value;
    Mel_Gpu_Pipeline_Create_Result cp = mel_gpu_pipeline_compute_create(dev, .shader = m->shader, .push_constant_size = sizeof(Mandel_Root), .name = "mandelbrot");
    if (mel_gpu_failed(cp.status))
        return m;
    m->pipeline = cp.value;

    if (!bindless_present_init(&m->present, dev, mel_gpu_swapchain_format(sc)))
        return m;

    Mel_Gpu_Swapchain_Extent ext = mel_gpu_swapchain_extent(sc);
    make_img(m, (i32)ext.width, (i32)ext.height);

    m->ready = true;
    return m;
}

static void mandel_resize(void* state, i32 w, i32 h)
{
    Mandel* m = state;
    if (!m->ready)
        return;
    make_img(m, w, h);
}

static void mandel_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Mandel* m = state;
    m->t += dt;

    if (!m->ready || handle_zero(m->img.slot))
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        hud_frame(&m->hud, dt, "mandelbrot (init)");
        return;
    }

    f64 cyc = fmod(m->t * 0.16, 6.2831853);
    f64 zoom = 0.5 - 0.5 * cos(cyc);
    f32 scale = (f32)(3.0 * exp(-zoom * 9.5));
    f32 cxp = -0.74364388703f;
    f32 cyp = 0.13182590421f;
    u32 iter = (u32)(MAX_ITER * (0.35 + 0.65 * zoom));

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    Mel_Gpu_Resource_State    src = m->img_fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    m->img_fresh = false;
    mel_gpu_cmd_texture_barrier(cmd, m->img, range, src, MEL_GPU_STATE_UNORDERED_ACCESS);

    Mandel_Root root = { .image = m->img_slot, .w = (u32)m->w, .h = (u32)m->h, .max_iter = iter, .center_x = cxp, .center_y = cyp, .scale = scale, .time = (f32)m->t };
    mel_gpu_cmd_bind_pipeline(cmd, m->pipeline);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_dispatch(cmd, ((u32)m->w + 7) / 8, ((u32)m->h + 7) / 8, 1);

    mel_gpu_cmd_texture_barrier(cmd, m->img, range, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);
    bindless_present_blit(&m->present, cmd, m->img_slot, mel_gpu_rgba(0, 0, 0, 1));

    hud_frame(&m->hud, dt, "mandelbrot · compute → storage image → bindless present");
}

static void mandel_teardown(void* state)
{
    Mandel* m = state;
    if (!m)
        return;
    if (m->ready)
    {
        destroy_img(m);
        bindless_present_teardown(&m->present);
        mel_gpu_pipeline_destroy(m->dev, m->pipeline);
        mel_gpu_shader_destroy(m->dev, m->shader);
    }
    free(m);
}

const Graphical_App MANDELBROT_APP = {
    .title = "mandelbrot-explorer",
    .init = mandel_init,
    .resize = mandel_resize,
    .render = mandel_render,
    .teardown = mandel_teardown,
};
