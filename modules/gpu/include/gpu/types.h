#pragma once

#include <core/types.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Swapchain    Mel_Gpu_Swapchain;
typedef struct Mel_Gpu_Buffer       Mel_Gpu_Buffer;
typedef struct Mel_Gpu_Shader       Mel_Gpu_Shader;
typedef struct Mel_Gpu_Pipeline     Mel_Gpu_Pipeline;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

typedef enum {
    MEL_GPU_FORMAT_UNDEFINED = 0,
    MEL_GPU_FORMAT_BGRA8_UNORM,
    MEL_GPU_FORMAT_RGBA8_UNORM,
    MEL_GPU_FORMAT_RG32_FLOAT,
    MEL_GPU_FORMAT_RGB32_FLOAT,
    MEL_GPU_FORMAT_RGBA32_FLOAT,
} Mel_Gpu_Format;

typedef enum {
    MEL_GPU_TOPOLOGY_TRIANGLE_LIST = 0,
    MEL_GPU_TOPOLOGY_TRIANGLE_STRIP,
    MEL_GPU_TOPOLOGY_LINE_LIST,
    MEL_GPU_TOPOLOGY_POINT_LIST,
} Mel_Gpu_Topology;

typedef enum {
    MEL_GPU_CULL_NONE = 0,
    MEL_GPU_CULL_FRONT,
    MEL_GPU_CULL_BACK,
} Mel_Gpu_Cull;

typedef enum {
    MEL_GPU_BUFFER_VERTEX  = 1u << 0,
    MEL_GPU_BUFFER_INDEX   = 1u << 1,
    MEL_GPU_BUFFER_UNIFORM = 1u << 2,
} Mel_Gpu_Buffer_Usage;

typedef enum {
    MEL_GPU_MEMORY_GPU_ONLY = 0, // device-local, not CPU-visible
    MEL_GPU_MEMORY_UPLOAD,       // CPU-writable, streamed to the GPU
} Mel_Gpu_Memory;

typedef struct { f32 r, g, b, a; } Mel_Gpu_Color;

static inline Mel_Gpu_Color mel_gpu_rgba(f32 r, f32 g, f32 b, f32 a)
{
    return (Mel_Gpu_Color){ r, g, b, a };
}

typedef struct {
    u32            location;
    Mel_Gpu_Format format;
    u32            offset;
} Mel_Gpu_Vertex_Element;
