#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "compute_plasma.h"

static const char COMPUTE_PLASMA_SLANG[] = {
#embed "shaders/slang/compute_plasma.slang"
    , 0
};

#define GRID_W     64
#define GRID_H     48
#define CELL_COUNT (GRID_W * GRID_H)

typedef struct
{
    u32 cells_rw;
    u32 cells_ro;
    u32 grid_w;
    u32 grid_h;
    f32 time;
    f32 pad;
} Plasma_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    Mel_Gpu_Shader   comp_shader;
    Mel_Gpu_Pipeline comp_pipeline;
    Mel_Gpu_Shader   draw_shader;
    Mel_Gpu_Pipeline draw_pipeline;
    Mel_Gpu_Buffer   cells;
    u32              cell_slot;
    bool             first_frame;
    f64              t;
} Plasma;

static void* plasma_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Plasma* p = calloc(1, sizeof *p);
    p->dev = dev;

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "compute-plasma: bindless heap unavailable; the storage-buffer heap class needs it");
        return p;
    }

    Mel_Gpu_Pipeline_From_Slang_Result cp = mel_gpu_pipeline_compute_create_from_slang(dev, .source = COMPUTE_PLASMA_SLANG, .compute_entry = "cs_main", .push_constant_size = sizeof(Plasma_Root), .bindless = true, .name = "plasma");
    if (mel_gpu_failed(cp.status))
        return p;
    p->comp_shader = cp.shader;
    p->comp_pipeline = cp.value;

    Mel_Gpu_Pipeline_From_Slang_Result dp = mel_gpu_pipeline_create_from_slang(dev,
                                                                               .source = COMPUTE_PLASMA_SLANG,
                                                                               .vertex_entry = "vs_cells",
                                                                               .fragment_entry = "fs_cells",
                                                                               .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                               .cull = MEL_GPU_CULL_NONE,
                                                                               .color_format = mel_gpu_swapchain_format(sc),
                                                                               .bindless = true,
                                                                               .name = "cells");
    if (mel_gpu_failed(dp.status))
        return p;
    p->draw_shader = dp.shader;
    p->draw_pipeline = dp.value;

    Mel_Gpu_Buffer_Create_Result cb = mel_gpu_buffer_create(dev, .size = (usize)CELL_COUNT * 4 * sizeof(f32), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "plasma-cells");
    if (mel_gpu_failed(cb.status))
        return p;
    p->cells = cb.value;
    p->cell_slot = mel_gpu_buffer_bindless_slot(dev, p->cells);

    p->first_frame = true;
    p->ready = true;
    return p;
}

static void plasma_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Plasma* p = state;
    p->t += dt;

    if (!p->ready)
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    Mel_Gpu_Resource_State buf_src = p->first_frame ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    p->first_frame = false;
    mel_gpu_cmd_buffer_barrier(cmd, p->cells, buf_src, MEL_GPU_STATE_UNORDERED_ACCESS);

    Plasma_Root croot = { .cells_rw = p->cell_slot, .cells_ro = p->cell_slot, .grid_w = GRID_W, .grid_h = GRID_H, .time = (f32)p->t };
    mel_gpu_cmd_bind_pipeline(cmd, p->comp_pipeline);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof croot, &croot);
    mel_gpu_cmd_dispatch(cmd, (GRID_W + 7) / 8, (GRID_H + 7) / 8, 1);

    mel_gpu_cmd_buffer_barrier(cmd, p->cells, MEL_GPU_STATE_UNORDERED_ACCESS, MEL_GPU_STATE_SHADER_RESOURCE);

    Plasma_Root droot = { .cells_rw = p->cell_slot, .cells_ro = p->cell_slot, .grid_w = GRID_W, .grid_h = GRID_H };
    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.02f, 0.02f, 0.03f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, p->draw_pipeline);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof droot, &droot);
    mel_gpu_cmd_draw(cmd, 6, CELL_COUNT);
    mel_gpu_cmd_end_pass(cmd);
}

static void plasma_teardown(void* state)
{
    Plasma* p = state;
    if (!p)
        return;
    if (p->ready)
    {
        mel_gpu_buffer_destroy(p->dev, p->cells);
        mel_gpu_pipeline_destroy(p->dev, p->draw_pipeline);
        mel_gpu_shader_destroy(p->dev, p->draw_shader);
        mel_gpu_pipeline_destroy(p->dev, p->comp_pipeline);
        mel_gpu_shader_destroy(p->dev, p->comp_shader);
    }
    free(p);
}

const Graphical_App COMPUTE_PLASMA_APP = {
    .title = "compute-plasma",
    .init = plasma_init,
    .render = plasma_render,
    .teardown = plasma_teardown,
};
