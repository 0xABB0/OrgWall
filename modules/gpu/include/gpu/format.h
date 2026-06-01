#pragma once

#include <core/types.h>

typedef enum
{
    MEL_GPU_FORMAT_UNDEFINED = 0,
    MEL_GPU_FORMAT_BGRA8_UNORM,
    MEL_GPU_FORMAT_RGBA8_UNORM,
    MEL_GPU_FORMAT_RGBA8_SRGB,
    MEL_GPU_FORMAT_BGRA8_SRGB,
    MEL_GPU_FORMAT_RG32_FLOAT,
    MEL_GPU_FORMAT_RGB32_FLOAT,
    MEL_GPU_FORMAT_RGBA32_FLOAT,
    MEL_GPU_FORMAT_D32_FLOAT,
    MEL_GPU_FORMAT_D24_UNORM_S8_UINT,
} Mel_Gpu_Format;

typedef struct
{
    f32 r, g, b, a;
} Mel_Gpu_Color;

static inline Mel_Gpu_Color mel_gpu_rgba(f32 r, f32 g, f32 b, f32 a) { return (Mel_Gpu_Color){ r, g, b, a }; }

u32  mel_gpu_format_bytes(Mel_Gpu_Format format);
bool mel_gpu_format_is_depth(Mel_Gpu_Format format);
