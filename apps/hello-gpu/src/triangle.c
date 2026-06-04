#include <stddef.h>
#include <stdlib.h>

#include "triangle.h"

static const char TRIANGLE_SLANG[] = {
#embed "shaders/slang/triangle.slang"
    ,
    0
};

typedef struct
{
    f32 pos[3];
    f32 color[4];
} Vertex;

typedef struct
{
    Mel_Gpu_Device*  dev;
    Mel_Gpu_Shader   shader;
    Mel_Gpu_Pipeline pipeline;
    Mel_Gpu_Buffer   vbo;
} Triangle;

static void* triangle_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Triangle* t = calloc(1, sizeof *t);
    t->dev = dev;

    const Vertex verts[] = {
        { { 0.0f, 0.6f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.6f, -0.6f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.6f, -0.6f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };

    t->vbo = mel_gpu_buffer_create(dev, .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .data = verts, .name = "triangle-vbo").value;

    Mel_Gpu_Pipeline_From_Slang_Result pipe = mel_gpu_pipeline_create_from_slang(dev,
                                                                                 .source = TRIANGLE_SLANG,
                                                                                 .vertex_entry = "vs_main",
                                                                                 .fragment_entry = "fs_main",
                                                                                 .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                 .cull = MEL_GPU_CULL_NONE,
                                                                                 .color_format = mel_gpu_swapchain_format(sc),
                                                                                 .name = "triangle");
    t->shader = pipe.shader;
    t->pipeline = pipe.value;

    return t;
}

static void triangle_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    (void)dt;
    Triangle* t = state;
    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.08f, 0.10f, 0.13f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, t->pipeline);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, t->vbo);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_pass(cmd);
}

static void triangle_teardown(void* state)
{
    Triangle* t = state;
    if (!t)
        return;
    mel_gpu_pipeline_destroy(t->dev, t->pipeline);
    mel_gpu_shader_destroy(t->dev, t->shader);
    mel_gpu_buffer_destroy(t->dev, t->vbo);
    free(t);
}

const Graphical_App TRIANGLE_APP = {
    .title = "hello-triangle",
    .init = triangle_init,
    .render = triangle_render,
    .teardown = triangle_teardown,
};
