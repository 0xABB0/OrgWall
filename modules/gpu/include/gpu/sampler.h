#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

// U11 (gpu-rhi.md §6.3). The direct handle carries the bindless slot (== handle index); the indirect peer
// is the compacted-heap / capped-bindless form. No foreign-sampler import exists — importing a sampler
// descriptor buys nothing — so the indirect family is engine-owned only.
MEL_GPU_HANDLE(Mel_Gpu_Sampler);
MEL_GPU_HANDLE_INDIRECT(Mel_Gpu_Sampler_Indirect);

// Sampler state is a closed protocol mapping onto every backend's fixed sampler descriptor (Vulkan / D3D12 /
// Metal / WebGPU); the enums below follow the same MEL-CODE-001 carve-out as the format / topology / state
// enums already in this module — protocol mapping, not an open abstraction.
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
    MEL_GPU_COMPARE_NONE = 0, // sampler is not a comparison/shadow sampler
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
    // gpu-rhi.md §6.7: the sampler's bindless slot (= its slotmap index, §3.1) exceeds the sampler heap class
    // capacity (the realistic trip wire — the sampler cap is the smallest). Surfaced loudly rather than
    // registering an unbound slot (CRITICAL-1 / MEL-ENGINE-VIII).
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
    f32                  max_anisotropy; // 0/1 disables; clamped to caps.sampler.max_anisotropy
    Mel_Gpu_Compare_Op   compare;        // NONE for a non-shadow sampler
    Mel_Gpu_Border_Color border;
    f32                  lod_min;
    f32                  lod_max; // 0 means "no upper clamp" -> VK_LOD_CLAMP_NONE
    const char*          name;
} Mel_Gpu_Sampler_Opt;

typedef struct
{
    Mel_Gpu_Sampler               value;
    Mel_Gpu_Sampler_Create_Status status;
} Mel_Gpu_Sampler_Create_Result;

// Auto-deduplicating: identical descriptors return the same shared handle, refcounted internally
// (gpu-rhi.md §6.3). Each create is one logical claim; destroy releases one.
Mel_Gpu_Sampler_Create_Result mel_gpu_sampler_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt);
#define mel_gpu_sampler_create(dev, ...) mel_gpu_sampler_create_opt((dev), (Mel_Gpu_Sampler_Opt){ __VA_ARGS__ })

void mel_gpu_sampler_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler);
bool mel_gpu_sampler_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler);

// The shader-visible heap slot. Equals the handle index by the §3.1 direct contract; query it rather than
// assuming, so code stays correct if the handle ever migrates to the indirect family.
u32 mel_gpu_sampler_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler);
