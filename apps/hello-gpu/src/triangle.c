#include <stddef.h>
#include <stdlib.h>

#include "triangle.h"
#include "triangle_spv.h"

typedef struct {
    f32 pos[3];
    f32 color[4];
} Vertex;

typedef struct {
    Mel_Gpu_Shader*   shader;
    Mel_Gpu_Pipeline* pipeline;
    Mel_Gpu_Buffer*   vbo;
} Triangle;

static const char* TRIANGLE_MSL =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VsIn  { float3 pos [[attribute(0)]]; float4 color [[attribute(1)]]; };\n"
    "struct VsOut { float4 pos [[position]]; float4 color; };\n"
    "vertex VsOut vs_main(VsIn in [[stage_in]]) {\n"
    "    VsOut o; o.pos = float4(in.pos, 1.0); o.color = in.color; return o;\n"
    "}\n"
    "fragment float4 fs_main(VsOut in [[stage_in]]) { return in.color; }\n";

static const char* TRIANGLE_WGSL =
    "struct VsOut { @builtin(position) pos: vec4<f32>, @location(0) color: vec4<f32> };\n"
    "@vertex fn vs_main(@location(0) pos: vec3<f32>, @location(1) color: vec4<f32>) -> VsOut {\n"
    "    var o: VsOut; o.pos = vec4<f32>(pos, 1.0); o.color = color; return o;\n"
    "}\n"
    "@fragment fn fs_main(in: VsOut) -> @location(0) vec4<f32> { return in.color; }\n";

static void* triangle_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Triangle* t = calloc(1, sizeof *t);

    const Vertex verts[] = {
        {{  0.0f,  0.6f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }},
        {{  0.6f, -0.6f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }},
        {{ -0.6f, -0.6f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }},
    };

    t->vbo = mel_gpu_buffer_create(dev,
        .size = sizeof verts, .usage = MEL_GPU_BUFFER_VERTEX,
        .memory = MEL_GPU_MEMORY_UPLOAD, .data = verts);

    t->shader = mel_gpu_shader_create(dev,
        .metal_source = TRIANGLE_MSL, .wgsl_source = TRIANGLE_WGSL,
        .spirv_vertex = TRIANGLE_VERT_SPV, .spirv_vertex_size = sizeof TRIANGLE_VERT_SPV,
        .spirv_fragment = TRIANGLE_FRAG_SPV, .spirv_fragment_size = sizeof TRIANGLE_FRAG_SPV,
        .vertex_entry = "vs_main", .fragment_entry = "fs_main");

    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT,  .offset = offsetof(Vertex, pos)   },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Vertex, color) },
    };

    t->pipeline = mel_gpu_pipeline_create(dev,
        .shader = t->shader,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .cull = MEL_GPU_CULL_NONE,
        .color_format = mel_gpu_swapchain_format(sc),
        .vertex_layout = layout, .vertex_layout_count = 2, .vertex_stride = sizeof(Vertex));

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
    if (!t) return;
    mel_gpu_pipeline_destroy(t->pipeline);
    mel_gpu_shader_destroy(t->shader);
    mel_gpu_buffer_destroy(t->vbo);
    free(t);
}

const Graphical_App TRIANGLE_APP = {
    .title    = "hello-triangle",
    .init     = triangle_init,
    .render   = triangle_render,
    .teardown = triangle_teardown,
};
