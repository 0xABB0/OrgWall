#include <math.h>
#include <stdlib.h>

#include "gallery.h"
#include "quad_spv.h"
#include "gradient_spv.h"
#include "blit_spv.h" // BLIT_VERT_SPV: shared fullscreen vertex stage

typedef struct
{
    f32 rect[4];
    f32 color[4];
} Quad_Root;

// Six gallery cells, each a label-free demonstration of one fill/blend pairing.
#define CELL_COUNT 6

typedef struct
{
    Mel_Gpu_Device*  dev;
    Mel_Gpu_Shader   bg_shader;
    Mel_Gpu_Pipeline bg_pipeline;
    Mel_Gpu_Shader   quad_shader;
    Mel_Gpu_Pipeline cells[CELL_COUNT];
    f64              t;
} Gallery;

static Mel_Gpu_Pipeline make_cell(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, Mel_Gpu_Format fmt, Mel_Gpu_Fill fill, Mel_Gpu_Blend blend)
{
    Mel_Gpu_Color_Target target = { .format = fmt, .blend = blend };
    return mel_gpu_pipeline_create(dev,
                                   .shader = sh,
                                   .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                   .cull = MEL_GPU_CULL_NONE,
                                   .fill = fill,
                                   .color_targets = &target,
                                   .color_target_count = 1,
                                   .push_constant_size = sizeof(Quad_Root),
                                   .name = "gallery-cell")
        .value;
}

static void* gallery_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Gallery* g = calloc(1, sizeof *g);
    g->dev = dev;
    Mel_Gpu_Format fmt = mel_gpu_swapchain_format(sc);

    g->bg_shader = mel_gpu_shader_create_from_bytecode(dev,
                                                       .spirv_vertex = BLIT_VERT_SPV,
                                                       .spirv_vertex_size = sizeof BLIT_VERT_SPV,
                                                       .spirv_fragment = GRADIENT_FRAG_SPV,
                                                       .spirv_fragment_size = sizeof GRADIENT_FRAG_SPV,
                                                       .vertex_entry = "main",
                                                       .fragment_entry = "main",
                                                       .name = "gallery-bg")
                       .value;
    g->bg_pipeline = mel_gpu_pipeline_create(dev, .shader = g->bg_shader, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = fmt, .name = "gallery-bg").value;

    g->quad_shader = mel_gpu_shader_create_from_bytecode(dev,
                                                         .spirv_vertex = QUAD_VERT_SPV,
                                                         .spirv_vertex_size = sizeof QUAD_VERT_SPV,
                                                         .spirv_fragment = QUAD_FRAG_SPV,
                                                         .spirv_fragment_size = sizeof QUAD_FRAG_SPV,
                                                         .vertex_entry = "main",
                                                         .fragment_entry = "main",
                                                         .name = "gallery-quad")
                         .value;

    Mel_Gpu_Blend additive = {
        .enable = true,
        .src_color = MEL_GPU_BLEND_ONE,
        .dst_color = MEL_GPU_BLEND_ONE,
        .color_op = MEL_GPU_BLEND_OP_ADD,
        .src_alpha = MEL_GPU_BLEND_ONE,
        .dst_alpha = MEL_GPU_BLEND_ONE,
        .alpha_op = MEL_GPU_BLEND_OP_ADD,
        .write_mask = MEL_GPU_COLOR_WRITE_ALL,
    };

    // Top row: solid fill — opaque, alpha, additive.
    g->cells[0] = make_cell(dev, g->quad_shader, fmt, MEL_GPU_FILL_SOLID, MEL_GPU_BLEND_OPAQUE);
    g->cells[1] = make_cell(dev, g->quad_shader, fmt, MEL_GPU_FILL_SOLID, MEL_GPU_BLEND_ALPHA);
    g->cells[2] = make_cell(dev, g->quad_shader, fmt, MEL_GPU_FILL_SOLID, additive);
    // Bottom row: wireframe fill (degrades to solid if unsupported) — same blends.
    g->cells[3] = make_cell(dev, g->quad_shader, fmt, MEL_GPU_FILL_WIREFRAME, MEL_GPU_BLEND_OPAQUE);
    g->cells[4] = make_cell(dev, g->quad_shader, fmt, MEL_GPU_FILL_WIREFRAME, MEL_GPU_BLEND_ALPHA);
    g->cells[5] = make_cell(dev, g->quad_shader, fmt, MEL_GPU_FILL_WIREFRAME, additive);
    return g;
}

static void gallery_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Gallery* g = state;
    g->t += dt;

    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0, 0, 0, 1));
    mel_gpu_cmd_bind_pipeline(cmd, g->bg_pipeline);
    mel_gpu_cmd_draw(cmd, 3, 1);

    // 3 columns x 2 rows. Each cell draws two overlapping quads so blend mode is
    // legible (the second over the first), with a gentle pulse.
    const f32 col_x[3] = { -0.62f, 0.0f, 0.62f };
    const f32 row_y[2] = { 0.42f, -0.42f };
    f32       pulse = 0.5f + 0.5f * sinf((f32)g->t * 1.5f);

    for (i32 i = 0; i < CELL_COUNT; ++i)
    {
        i32 cx = i % 3;
        i32 cy = i / 3;
        f32 ox = col_x[cx];
        f32 oy = row_y[cy];

        mel_gpu_cmd_bind_pipeline(cmd, g->cells[i]);

        Quad_Root base = { .rect = { ox - 0.06f, oy, 0.22f, 0.22f }, .color = { 0.20f, 0.55f, 0.90f, 0.6f } };
        mel_gpu_cmd_push_constants(cmd, 0, sizeof base, &base);
        mel_gpu_cmd_draw(cmd, 6, 1);

        Quad_Root over = { .rect = { ox + 0.06f, oy, 0.22f, 0.22f }, .color = { 0.95f, 0.45f, 0.15f, 0.4f + 0.4f * pulse } };
        mel_gpu_cmd_push_constants(cmd, 0, sizeof over, &over);
        mel_gpu_cmd_draw(cmd, 6, 1);
    }

    mel_gpu_cmd_end_pass(cmd);
}

static void gallery_teardown(void* state)
{
    Gallery* g = state;
    if (!g)
        return;
    for (i32 i = 0; i < CELL_COUNT; ++i)
        mel_gpu_pipeline_destroy(g->dev, g->cells[i]);
    mel_gpu_shader_destroy(g->dev, g->quad_shader);
    mel_gpu_pipeline_destroy(g->dev, g->bg_pipeline);
    mel_gpu_shader_destroy(g->dev, g->bg_shader);
    free(g);
}

const Graphical_App GALLERY_APP = {
    .title = "fill-blend-gallery",
    .init = gallery_init,
    .render = gallery_render,
    .teardown = gallery_teardown,
};
