#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "texquad.h"
#include "bindless_present.h"

#define TEX_SIZE 512

typedef struct
{
    Mel_Gpu_Device*      dev;
    bool                 ready; // bindless heap present and resources built
    Mel_Gpu_Texture      tex;
    Mel_Gpu_Texture_View view;
    u32                  tex_slot;
    Bindless_Present     present;
} Texquad;

// Escape-time Mandelbrot, coloured by a smooth iteration count. Filled once on
// the CPU and uploaded — the texture is the whole point, the heap is how the
// shader reaches it.
static void fill_mandelbrot(u8* px, u32 w, u32 h)
{
    for (u32 y = 0; y < h; ++y)
    {
        for (u32 x = 0; x < w; ++x)
        {
            f64       cr = -2.2 + 3.0 * (f64)x / (f64)w;
            f64       ci = -1.5 + 3.0 * (f64)y / (f64)h;
            f64       zr = 0, zi = 0;
            u32       it = 0;
            const u32 max_it = 256;
            while (zr * zr + zi * zi <= 4.0 && it < max_it)
            {
                f64 nr = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = nr;
                ++it;
            }

            f32 r, g, b;
            if (it >= max_it)
            {
                r = g = b = 0.0f;
            }
            else
            {
                f32 t = (f32)it / (f32)max_it;
                f32 s = sqrtf(t);
                r = 0.5f + 0.5f * cosf(6.2831f * (s + 0.00f));
                g = 0.5f + 0.5f * cosf(6.2831f * (s + 0.33f));
                b = 0.5f + 0.5f * cosf(6.2831f * (s + 0.67f));
            }

            u8* p = px + ((usize)y * w + x) * 4;
            p[0] = (u8)(r * 255.0f);
            p[1] = (u8)(g * 255.0f);
            p[2] = (u8)(b * 255.0f);
            p[3] = 255;
        }
    }
}

static void* texquad_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Texquad* t = calloc(1, sizeof *t);
    t->dev = dev;

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "texquad: bindless heap unavailable; showing a notice colour (device lacks descriptor_indexing)");
        return t;
    }

    Mel_Gpu_Texture_Create_Result tex = mel_gpu_texture_create(dev,
                                                               .kind = MEL_GPU_TEXTURE_2D,
                                                               .extent = { TEX_SIZE, TEX_SIZE, 1 },
                                                               .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                               .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST,
                                                               .name = "mandelbrot");
    if (mel_gpu_failed(tex.status))
        return t;
    t->tex = tex.value;

    u8* px = malloc((usize)TEX_SIZE * TEX_SIZE * 4);
    fill_mandelbrot(px, TEX_SIZE, TEX_SIZE);
    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { TEX_SIZE, TEX_SIZE, 1 } };
    mel_gpu_texture_write(dev, t->tex, region, px, (usize)TEX_SIZE * TEX_SIZE * 4);
    free(px);

    Mel_Gpu_Texture_View_Create_Result view = mel_gpu_texture_default_view(dev, t->tex);
    if (mel_gpu_failed(view.status))
        return t;
    t->view = view.value;
    t->tex_slot = mel_gpu_texture_view_bindless_slot(dev, t->view);

    if (!bindless_present_init(&t->present, dev, mel_gpu_swapchain_format(sc)))
        return t;

    t->ready = true;
    return t;
}

static void texquad_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    (void)dt;
    Texquad* t = state;
    if (!t->ready)
    {
        // Graceful notice: a deep-amber clear stands in for "bindless absent".
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }
    bindless_present_blit(&t->present, cmd, t->tex_slot, mel_gpu_rgba(0, 0, 0, 1));
}

static void texquad_teardown(void* state)
{
    Texquad* t = state;
    if (!t)
        return;
    if (t->ready)
    {
        bindless_present_teardown(&t->present);
        mel_gpu_texture_view_destroy(t->dev, t->view);
        mel_gpu_texture_destroy(t->dev, t->tex);
    }
    free(t);
}

const Graphical_App TEXQUAD_APP = {
    .title = "bindless-textured-quad",
    .init = texquad_init,
    .render = texquad_render,
    .teardown = texquad_teardown,
};
