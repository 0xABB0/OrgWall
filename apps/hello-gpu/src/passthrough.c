#include <stddef.h>

#include "passthrough.h"

static const char PASSTHROUGH_SLANG[] = {
#embed "shaders/slang/passthrough.slang"
    , 0
};

Mel_Gpu_Shader passthrough_shader(Mel_Gpu_Device* dev)
{
    return mel_gpu_shader_create_from_slang(dev,
                                            .source = PASSTHROUGH_SLANG,
                                            .vertex_entry = "vs_main",
                                            .fragment_entry = "fs_main",
                                            .name = "passthrough")
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
