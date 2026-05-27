#pragma once

#include <gpu/gpu.h>

typedef struct {
    f32 pos[3];
    f32 color[4];
} Pt_Vertex;

Mel_Gpu_Shader*   passthrough_shader(Mel_Gpu_Device* dev);
Mel_Gpu_Pipeline* passthrough_pipeline(Mel_Gpu_Device* dev, Mel_Gpu_Shader* sh,
                                       Mel_Gpu_Topology topology, Mel_Gpu_Format color_format);
