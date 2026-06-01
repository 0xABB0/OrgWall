#include <stddef.h>

#include "passthrough.h"
#include "triangle_spv.h"

Mel_Gpu_Shader passthrough_shader(Mel_Gpu_Device* dev)
{
    return mel_gpu_shader_create_from_bytecode(dev,
                                               .spirv_vertex = TRIANGLE_VERT_SPV,
                                               .spirv_vertex_size = sizeof TRIANGLE_VERT_SPV,
                                               .spirv_fragment = TRIANGLE_FRAG_SPV,
                                               .spirv_fragment_size = sizeof TRIANGLE_FRAG_SPV,
                                               .vertex_entry = "vs_main",
                                               .fragment_entry = "fs_main")
        .value;
}

Mel_Gpu_Pipeline passthrough_pipeline(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, Mel_Gpu_Topology topology, Mel_Gpu_Format color_format)
{
    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Pt_Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Pt_Vertex, color) },
    };

    return mel_gpu_pipeline_create(dev, .shader = sh, .topology = topology, .cull = MEL_GPU_CULL_NONE, .color_format = color_format, .vertex_layout = layout, .vertex_layout_count = 2, .vertex_stride = sizeof(Pt_Vertex)).value;
}
