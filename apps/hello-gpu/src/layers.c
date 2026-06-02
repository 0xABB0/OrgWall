#include <math.h>
#include <stdlib.h>

#include "layers.h"
#include "quad_spv.h"
#include "gradient_spv.h"
#include "blit_spv.h" // BLIT_VERT_SPV: the shared attributeless fullscreen vertex stage

#define LAYER_COUNT 5

typedef struct
{
    f32 rect[4];  // centre.xy, half-extent.xy (NDC)
    f32 color[4]; // rgb + alpha
} Quad_Root;

typedef struct
{
    Mel_Gpu_Device*  dev;
    Mel_Gpu_Shader   bg_shader;     // fullscreen vert + gradient frag
    Mel_Gpu_Pipeline bg_pipeline;   // opaque gradient backdrop
    Mel_Gpu_Shader   quad_shader;   // quad vert + solid frag
    Mel_Gpu_Pipeline quad_pipeline; // alpha-blended translucent quads
    f64              t;
} Layers;

static void* layers_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Layers* l = calloc(1, sizeof *l);
    l->dev = dev;
    Mel_Gpu_Format fmt = mel_gpu_swapchain_format(sc);

    l->bg_shader = mel_gpu_shader_create_from_bytecode(dev,
                                                       .spirv_vertex = BLIT_VERT_SPV,
                                                       .spirv_vertex_size = sizeof BLIT_VERT_SPV,
                                                       .spirv_fragment = GRADIENT_FRAG_SPV,
                                                       .spirv_fragment_size = sizeof GRADIENT_FRAG_SPV,
                                                       .vertex_entry = "main",
                                                       .fragment_entry = "main",
                                                       .name = "gradient")
                       .value;
    l->bg_pipeline = mel_gpu_pipeline_create(dev, .shader = l->bg_shader, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = fmt, .name = "gradient").value;

    l->quad_shader = mel_gpu_shader_create_from_bytecode(dev,
                                                         .spirv_vertex = QUAD_VERT_SPV,
                                                         .spirv_vertex_size = sizeof QUAD_VERT_SPV,
                                                         .spirv_fragment = QUAD_FRAG_SPV,
                                                         .spirv_fragment_size = sizeof QUAD_FRAG_SPV,
                                                         .vertex_entry = "main",
                                                         .fragment_entry = "main",
                                                         .name = "quad")
                         .value;
    Mel_Gpu_Color_Target target = { .format = fmt, .blend = MEL_GPU_BLEND_ALPHA };
    l->quad_pipeline = mel_gpu_pipeline_create(dev,
                                               .shader = l->quad_shader,
                                               .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                               .cull = MEL_GPU_CULL_NONE,
                                               .color_targets = &target,
                                               .color_target_count = 1,
                                               .push_constant_size = sizeof(Quad_Root),
                                               .name = "quad-alpha")
                           .value;
    return l;
}

static void layers_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Layers* l = state;
    l->t += dt;

    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0, 0, 0, 1));

    // Opaque gradient backdrop.
    mel_gpu_cmd_bind_pipeline(cmd, l->bg_pipeline);
    mel_gpu_cmd_draw(cmd, 3, 1);

    // Five drifting translucent quads, each a different hue. src-over over the
    // backdrop and over one another (MEL_GPU_BLEND_ALPHA).
    mel_gpu_cmd_bind_pipeline(cmd, l->quad_pipeline);
    static const f32 hue[LAYER_COUNT][3] = {
        { 0.95f, 0.26f, 0.21f }, { 0.18f, 0.80f, 0.44f }, { 0.20f, 0.60f, 0.86f }, { 0.95f, 0.77f, 0.06f }, { 0.61f, 0.35f, 0.71f },
    };
    for (i32 i = 0; i < LAYER_COUNT; ++i)
    {
        f32       phase = (f32)i / (f32)LAYER_COUNT * 6.2831853f;
        f32       drift = (f32)l->t * 0.6f + phase;
        Quad_Root root = {
            .rect = { 0.45f * cosf(drift), 0.45f * sinf(drift * 0.8f), 0.30f, 0.30f },
            .color = { hue[i][0], hue[i][1], hue[i][2], 0.45f },
        };
        mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
        mel_gpu_cmd_draw(cmd, 6, 1);
    }

    mel_gpu_cmd_end_pass(cmd);
}

static void layers_teardown(void* state)
{
    Layers* l = state;
    if (!l)
        return;
    mel_gpu_pipeline_destroy(l->dev, l->quad_pipeline);
    mel_gpu_shader_destroy(l->dev, l->quad_shader);
    mel_gpu_pipeline_destroy(l->dev, l->bg_pipeline);
    mel_gpu_shader_destroy(l->dev, l->bg_shader);
    free(l);
}

const Graphical_App LAYERS_APP = {
    .title = "alpha-blended-layers",
    .init = layers_init,
    .render = layers_render,
    .teardown = layers_teardown,
};
