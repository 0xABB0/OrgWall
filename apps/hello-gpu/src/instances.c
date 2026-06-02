#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "instances.h"
#include "instances_spv.h"

#define INSTANCE_COUNT 256
#define VBO_FRAMES     3

typedef struct
{
    f32 pos_scale[4];
    f32 color[4];
} Instance;

typedef struct
{
    u32 instance_buf;
    f32 aspect;
} Inst_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    bool             ready;
    Mel_Gpu_Shader   shader;
    Mel_Gpu_Pipeline pipeline;
    Mel_Gpu_Buffer   ssbo[VBO_FRAMES];
    u32              slot[VBO_FRAMES];
    i32              frame;
    f32              aspect;
    f64              t;
} Instances;

static void* instances_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Instances* in = calloc(1, sizeof *in);
    in->dev = dev;
    in->aspect = 1.0f;

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "instances: bindless heap unavailable; the per-instance store needs it");
        return in;
    }

    in->shader = mel_gpu_shader_create_from_bytecode(dev,
                                                     .spirv_vertex = INSTANCES_VERT_SPV,
                                                     .spirv_vertex_size = sizeof INSTANCES_VERT_SPV,
                                                     .spirv_fragment = INSTANCES_FRAG_SPV,
                                                     .spirv_fragment_size = sizeof INSTANCES_FRAG_SPV,
                                                     .vertex_entry = "main",
                                                     .fragment_entry = "main",
                                                     .name = "instances")
                     .value;
    in->pipeline = mel_gpu_pipeline_create(dev, .shader = in->shader, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = mel_gpu_swapchain_format(sc), .name = "instances").value;

    for (i32 i = 0; i < VBO_FRAMES; ++i)
    {
        Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(dev, .size = INSTANCE_COUNT * sizeof(Instance), .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "instance-store");
        if (mel_gpu_failed(b.status))
            return in;
        in->ssbo[i] = b.value;
        in->slot[i] = mel_gpu_buffer_bindless_slot(dev, in->ssbo[i]);
    }

    in->ready = true;
    return in;
}

static void instances_resize(void* state, i32 w, i32 h)
{
    Instances* in = state;
    in->aspect = (h > 0) ? (f32)w / (f32)h : 1.0f;
}

static void instances_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Instances* in = state;
    in->t += dt;

    if (!in->ready)
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    Instance* items = malloc(INSTANCE_COUNT * sizeof(Instance));
    for (i32 i = 0; i < INSTANCE_COUNT; ++i)
    {
        f32 fi = (f32)i;
        f32 a = fi * 2.3999632f + (f32)in->t * 0.4f;
        f32 r = 0.05f + 0.9f * sqrtf(fi / (f32)INSTANCE_COUNT);
        f32 x = r * cosf(a);
        f32 y = r * sinf(a);
        f32 size = 0.02f + 0.03f * (0.5f + 0.5f * sinf((f32)in->t * 2.0f + fi * 0.1f));
        f32 hue = fi / (f32)INSTANCE_COUNT;
        items[i] = (Instance){
            .pos_scale = { x, y, size, 0.0f },
            .color = {
                0.5f + 0.5f * cosf(6.2831f * (hue + 0.00f)),
                0.5f + 0.5f * cosf(6.2831f * (hue + 0.33f)),
                0.5f + 0.5f * cosf(6.2831f * (hue + 0.67f)),
                1.0f,
            },
        };
    }
    in->frame = (in->frame + 1) % VBO_FRAMES;
    mel_gpu_buffer_write(in->dev, in->ssbo[in->frame], items, INSTANCE_COUNT * sizeof(Instance));
    free(items);

    Inst_Root root = { .instance_buf = in->slot[in->frame], .aspect = in->aspect };

    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.03f, 0.03f, 0.05f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, in->pipeline);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 6, INSTANCE_COUNT);
    mel_gpu_cmd_end_pass(cmd);
}

static void instances_teardown(void* state)
{
    Instances* in = state;
    if (!in)
        return;
    if (in->ready)
    {
        for (i32 i = 0; i < VBO_FRAMES; ++i)
            mel_gpu_buffer_destroy(in->dev, in->ssbo[i]);
        mel_gpu_pipeline_destroy(in->dev, in->pipeline);
        mel_gpu_shader_destroy(in->dev, in->shader);
    }
    free(in);
}

const Graphical_App INSTANCES_APP = {
    .title = "instancing",
    .init = instances_init,
    .resize = instances_resize,
    .render = instances_render,
    .teardown = instances_teardown,
};
