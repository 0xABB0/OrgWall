#include <stddef.h>

#include "passthrough.h"
#include "triangle_spv.h"

static const char* PASSTHROUGH_MSL =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VsIn  { float3 pos [[attribute(0)]]; float4 color [[attribute(1)]]; };\n"
    "struct VsOut { float4 pos [[position]]; float4 color; };\n"
    "vertex VsOut vs_main(VsIn in [[stage_in]]) {\n"
    "    VsOut o; o.pos = float4(in.pos, 1.0); o.color = in.color; return o;\n"
    "}\n"
    "fragment float4 fs_main(VsOut in [[stage_in]]) { return in.color; }\n";

static const char* PASSTHROUGH_WGSL =
    "struct VsOut { @builtin(position) pos: vec4<f32>, @location(0) color: vec4<f32> };\n"
    "@vertex fn vs_main(@location(0) pos: vec3<f32>, @location(1) color: vec4<f32>) -> VsOut {\n"
    "    var o: VsOut; o.pos = vec4<f32>(pos, 1.0); o.color = color; return o;\n"
    "}\n"
    "@fragment fn fs_main(in: VsOut) -> @location(0) vec4<f32> { return in.color; }\n";

Mel_Gpu_Shader* passthrough_shader(Mel_Gpu_Device* dev)
{
    return mel_gpu_shader_create(dev,
        .metal_source = PASSTHROUGH_MSL, .wgsl_source = PASSTHROUGH_WGSL,
        .spirv_vertex = TRIANGLE_VERT_SPV, .spirv_vertex_size = sizeof TRIANGLE_VERT_SPV,
        .spirv_fragment = TRIANGLE_FRAG_SPV, .spirv_fragment_size = sizeof TRIANGLE_FRAG_SPV,
        .vertex_entry = "vs_main", .fragment_entry = "fs_main");
}

Mel_Gpu_Pipeline* passthrough_pipeline(Mel_Gpu_Device* dev, Mel_Gpu_Shader* sh,
                                       Mel_Gpu_Topology topology, Mel_Gpu_Format color_format)
{
    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT,  .offset = offsetof(Pt_Vertex, pos)   },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Pt_Vertex, color) },
    };

    return mel_gpu_pipeline_create(dev,
        .shader = sh,
        .topology = topology,
        .cull = MEL_GPU_CULL_NONE,
        .color_format = color_format,
        .vertex_layout = layout, .vertex_layout_count = 2, .vertex_stride = sizeof(Pt_Vertex));
}
