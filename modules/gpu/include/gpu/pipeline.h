#pragma once

#include <gpu/types.h>

typedef struct {
    Mel_Gpu_Shader*               shader;
    Mel_Gpu_Topology              topology;
    Mel_Gpu_Cull                  cull;
    Mel_Gpu_Format                color_format; // render target format (match the swapchain)
    const Mel_Gpu_Vertex_Element* vertex_layout;
    u32                           vertex_layout_count;
    u32                           vertex_stride;
} Mel_Gpu_Pipeline_Opt;

Mel_Gpu_Pipeline* mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt);
#define mel_gpu_pipeline_create(dev, ...) mel_gpu_pipeline_create_opt((dev), (Mel_Gpu_Pipeline_Opt){__VA_ARGS__})

void mel_gpu_pipeline_destroy(Mel_Gpu_Pipeline* pipe);
