#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>
#include <gpu/format.h>
#include <gpu/shader.h>
#include <gpu/sampler.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

MEL_GPU_HANDLE(Mel_Gpu_Pipeline);

// U11 immutable/static sampler baked into the pipeline layout (gpu-rhi.md §6.3 / §6.7). The referenced
// sampler must stay alive for the pipeline's lifetime.
typedef struct
{
    Mel_Gpu_Sampler sampler;
    u32             binding;
} Mel_Gpu_Static_Sampler;

typedef enum
{
    MEL_GPU_TOPOLOGY_TRIANGLE_LIST = 0,
    MEL_GPU_TOPOLOGY_TRIANGLE_STRIP,
    MEL_GPU_TOPOLOGY_LINE_LIST,
    MEL_GPU_TOPOLOGY_POINT_LIST,
} Mel_Gpu_Topology;

typedef enum
{
    MEL_GPU_CULL_NONE = 0,
    MEL_GPU_CULL_FRONT,
    MEL_GPU_CULL_BACK,
} Mel_Gpu_Cull;

typedef struct
{
    u32            location;
    Mel_Gpu_Format format;
    u32            offset;
} Mel_Gpu_Vertex_Element;

typedef enum
{
    MEL_GPU_PIPELINE_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_PIPELINE_CREATE_VK_FAILED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_PIPELINE_CREATE_NO_SHADER = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    // gpu-rhi.md §6.7: a pipeline whose layout demands a bindless heap the device cannot provide vs. a
    // requested heap slot beyond the heap's capacity — distinct remedies, never conflated (MEL-ENGINE-VIII).
    MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_PIPELINE_CREATE_MISSING_BINDLESS_SLOT = MEL_GPU_STATUS(4, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Pipeline_Create_Status;

typedef struct
{
    Mel_Gpu_Shader                shader;
    Mel_Gpu_Topology              topology;
    Mel_Gpu_Cull                  cull;
    Mel_Gpu_Format                color_format;
    Mel_Gpu_Format                depth_format;
    const Mel_Gpu_Vertex_Element* vertex_layout;
    u32                           vertex_layout_count;
    u32                           vertex_stride;
    u32                           push_constant_size;
    // U14: when true, set 0 of the layout is the device bindless heap, so the shader can index its texture /
    // sampler / buffer arrays. The per-draw root record rides the push-constant range above.
    bool                          bindless;
    const Mel_Gpu_Static_Sampler* static_samplers;
    u32                           static_sampler_count;
    const char*                   name;
} Mel_Gpu_Pipeline_Opt;

typedef struct
{
    Mel_Gpu_Pipeline               value;
    Mel_Gpu_Pipeline_Create_Status status;
} Mel_Gpu_Pipeline_Create_Result;

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt);
#define mel_gpu_pipeline_create(dev, ...) mel_gpu_pipeline_create_opt((dev), (Mel_Gpu_Pipeline_Opt){ __VA_ARGS__ })

void mel_gpu_pipeline_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe);
bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe);
