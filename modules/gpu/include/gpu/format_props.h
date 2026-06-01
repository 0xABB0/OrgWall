#pragma once

#include <core/types.h>
#include <gpu/format.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

typedef enum
{
    MEL_GPU_TILING_OPTIMAL = 0,
    MEL_GPU_TILING_LINEAR = 1,
} Mel_Gpu_Tiling;

enum
{
    MEL_GPU_FMT_SAMPLED = 1u << 0,
    MEL_GPU_FMT_STORAGE = 1u << 1,
    MEL_GPU_FMT_STORAGE_ATOMIC = 1u << 2,
    MEL_GPU_FMT_COLOR_ATTACHMENT = 1u << 3,
    MEL_GPU_FMT_COLOR_BLEND = 1u << 4,
    MEL_GPU_FMT_DEPTH_ATTACHMENT = 1u << 5,
    MEL_GPU_FMT_BLIT_SRC = 1u << 6,
    MEL_GPU_FMT_BLIT_DST = 1u << 7,
    MEL_GPU_FMT_LINEAR_FILTER = 1u << 8,
    MEL_GPU_FMT_TRANSFER_SRC = 1u << 9,
    MEL_GPU_FMT_TRANSFER_DST = 1u << 10,
    MEL_GPU_FMT_VERTEX_BUFFER = 1u << 11,
};

typedef struct
{
    u32 tiling_features;
    u32 linear_tiling_features;
    u32 buffer_features;
    u32 sample_counts;
} Mel_Gpu_Format_Properties;

Mel_Gpu_Format_Properties mel_gpu_format_properties(Mel_Gpu_Device* dev, Mel_Gpu_Format format, Mel_Gpu_Tiling tiling);
