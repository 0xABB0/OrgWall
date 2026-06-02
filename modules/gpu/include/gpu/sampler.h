#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

MEL_GPU_HANDLE(Mel_Gpu_Sampler);
MEL_GPU_HANDLE_INDIRECT(Mel_Gpu_Sampler_Indirect);

typedef enum
{
    MEL_GPU_FILTER_NEAREST = 0,
    MEL_GPU_FILTER_LINEAR,
} Mel_Gpu_Filter;

typedef enum
{
    MEL_GPU_MIPMAP_NEAREST = 0,
    MEL_GPU_MIPMAP_LINEAR,
} Mel_Gpu_Mipmap_Mode;

typedef enum
{
    MEL_GPU_WRAP_REPEAT = 0,
    MEL_GPU_WRAP_MIRROR_REPEAT,
    MEL_GPU_WRAP_CLAMP_EDGE,
    MEL_GPU_WRAP_CLAMP_BORDER,
} Mel_Gpu_Wrap;

typedef enum
{
    MEL_GPU_COMPARE_NONE = 0,
    MEL_GPU_COMPARE_NEVER,
    MEL_GPU_COMPARE_LESS,
    MEL_GPU_COMPARE_EQUAL,
    MEL_GPU_COMPARE_LESS_EQUAL,
    MEL_GPU_COMPARE_GREATER,
    MEL_GPU_COMPARE_NOT_EQUAL,
    MEL_GPU_COMPARE_GREATER_EQUAL,
    MEL_GPU_COMPARE_ALWAYS,
} Mel_Gpu_Compare_Op;

typedef enum
{
    MEL_GPU_BORDER_TRANSPARENT_BLACK = 0,
    MEL_GPU_BORDER_OPAQUE_BLACK,
    MEL_GPU_BORDER_OPAQUE_WHITE,
} Mel_Gpu_Border_Color;

typedef enum
{
    MEL_GPU_SAMPLER_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_SAMPLER_CREATE_VK_FAILED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_SAMPLER_CREATE_BAD_PARAMS = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_SAMPLER_CREATE_BINDLESS_SLOT_EXHAUSTED = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Sampler_Create_Status;

typedef struct
{
    Mel_Gpu_Filter       min_filter;
    Mel_Gpu_Filter       mag_filter;
    Mel_Gpu_Mipmap_Mode  mip_filter;
    Mel_Gpu_Wrap         wrap_u;
    Mel_Gpu_Wrap         wrap_v;
    Mel_Gpu_Wrap         wrap_w;
    f32                  max_anisotropy;
    Mel_Gpu_Compare_Op   compare;
    Mel_Gpu_Border_Color border;
    f32                  lod_min;
    f32                  lod_max;
    const char*          name;
} Mel_Gpu_Sampler_Opt;

typedef struct
{
    Mel_Gpu_Sampler               value;
    Mel_Gpu_Sampler_Create_Status status;
} Mel_Gpu_Sampler_Create_Result;

Mel_Gpu_Sampler_Create_Result mel_gpu_sampler_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt);
#define mel_gpu_sampler_create(dev, ...) mel_gpu_sampler_create_opt((dev), (Mel_Gpu_Sampler_Opt){ __VA_ARGS__ })

void mel_gpu_sampler_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler);
bool mel_gpu_sampler_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler);

u32 mel_gpu_sampler_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler);
