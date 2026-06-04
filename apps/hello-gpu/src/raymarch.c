#include <stdlib.h>

#include "raymarch.h"
#include "hud.h"

static const char RAYMARCH_SLANG[] = {
#embed "shaders/slang/raymarch.slang"
    , 0
};

typedef struct
{
    f32 time;
    f32 aspect;
    f32 pad0;
    f32 pad1;
} Raymarch_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    Mel_Gpu_Shader   shader;
    Mel_Gpu_Pipeline pipeline;
    f32              aspect;
    Hud              hud;
    f64              t;
} Raymarch;

static void* raymarch_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Raymarch* r = calloc(1, sizeof *r);
    r->dev = dev;
    r->aspect = 1.0f;
    hud_init(&r->hud, dev);

    Mel_Gpu_Pipeline_From_Slang_Result pl = mel_gpu_pipeline_create_from_slang(dev,
                                                                              .source = RAYMARCH_SLANG,
                                                                              .vertex_entry = "vs_main",
                                                                              .fragment_entry = "fs_main",
                                                                              .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                              .cull = MEL_GPU_CULL_NONE,
                                                                              .color_format = mel_gpu_swapchain_format(sc),
                                                                              .name = "raymarch");
    if (mel_gpu_failed(pl.status))
        return r;
    r->shader = pl.shader;
    r->pipeline = pl.value;

    r->ready = true;
    return r;
}

static void raymarch_resize(void* state, i32 w, i32 h)
{
    Raymarch* r = state;
    r->aspect = (h > 0) ? (f32)w / (f32)h : 1.0f;
}

static void raymarch_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Raymarch* r = state;
    r->t += dt;
    hud_frame(&r->hud, dt, "raymarched SDF · soft shadows + AO · orbiting cam");

    if (!r->ready)
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    Raymarch_Root root = { .time = (f32)r->t, .aspect = r->aspect };
    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0, 0, 0, 1));
    mel_gpu_cmd_bind_pipeline(cmd, r->pipeline);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_pass(cmd);
}

static void raymarch_teardown(void* state)
{
    Raymarch* r = state;
    if (!r)
        return;
    if (r->ready)
    {
        mel_gpu_pipeline_destroy(r->dev, r->pipeline);
        mel_gpu_shader_destroy(r->dev, r->shader);
    }
    free(r);
}

const Graphical_App RAYMARCH_APP = {
    .title = "raymarch-sdf",
    .init = raymarch_init,
    .resize = raymarch_resize,
    .render = raymarch_render,
    .teardown = raymarch_teardown,
};
