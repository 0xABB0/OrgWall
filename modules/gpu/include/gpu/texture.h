#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>
#include <gpu/format.h>
#include <gpu/memory.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

MEL_GPU_HANDLE(Mel_Gpu_Texture);
MEL_GPU_HANDLE(Mel_Gpu_Texture_View);

typedef enum
{
    MEL_GPU_TEXTURE_1D = 0,
    MEL_GPU_TEXTURE_2D = 1,
    MEL_GPU_TEXTURE_3D = 2,
} Mel_Gpu_Texture_Kind;

typedef enum
{
    MEL_GPU_TEXTURE_SAMPLED = 1u << 0,
    MEL_GPU_TEXTURE_STORAGE = 1u << 1,
    MEL_GPU_TEXTURE_ATTACHMENT = 1u << 2,
    MEL_GPU_TEXTURE_COPY_SRC = 1u << 3,
    MEL_GPU_TEXTURE_COPY_DST = 1u << 4,
    MEL_GPU_TEXTURE_TRANSIENT = 1u << 5,
} Mel_Gpu_Texture_Usage;

typedef enum
{
    MEL_GPU_VIEW_1D = 0,
    MEL_GPU_VIEW_1D_ARRAY,
    MEL_GPU_VIEW_2D,
    MEL_GPU_VIEW_2D_ARRAY,
    MEL_GPU_VIEW_3D,
    MEL_GPU_VIEW_CUBE,
    MEL_GPU_VIEW_CUBE_ARRAY,
} Mel_Gpu_View_Dimension;

typedef enum
{
    MEL_GPU_ASPECT_DEFAULT = 0,
    MEL_GPU_ASPECT_COLOR,
    MEL_GPU_ASPECT_DEPTH,
    MEL_GPU_ASPECT_STENCIL,
    MEL_GPU_ASPECT_DEPTH_AND_STENCIL,
    MEL_GPU_ASPECT_PLANE0,
    MEL_GPU_ASPECT_PLANE1,
    MEL_GPU_ASPECT_PLANE2,
} Mel_Gpu_Texture_Aspect;

typedef struct
{
    u32 width;
    u32 height;
    u32 depth;
} Mel_Gpu_Extent3;

typedef struct
{
    Mel_Gpu_Texture_Aspect aspect;
    u32                    base_mip;
    u32                    mip_count;
    u32                    base_layer;
    u32                    layer_count;
} Mel_Gpu_Subresource_Range;

typedef enum
{
    MEL_GPU_TEXTURE_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_TEXTURE_CREATE_OOM = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_TEXTURE_CREATE_VK_FAILED = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_TEXTURE_CREATE_BAD_PARAMS = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Texture_Create_Status;

typedef struct
{
    Mel_Gpu_Texture_Kind  kind;
    Mel_Gpu_Extent3       extent;
    u32                   array_layers;
    Mel_Gpu_Format        format;
    u32                   mip_levels;
    u32                   sample_count;
    Mel_Gpu_Texture_Usage usage;
    Mel_Gpu_Memory_Role   memory;
    bool                  cube_compatible;
    bool                  capture_replay;
    const char*           name;
} Mel_Gpu_Texture_Opt;

typedef struct
{
    Mel_Gpu_Texture               value;
    Mel_Gpu_Texture_Create_Status status;
} Mel_Gpu_Texture_Create_Result;

Mel_Gpu_Texture_Create_Result mel_gpu_texture_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Texture_Opt opt);
#define mel_gpu_texture_create(dev, ...) mel_gpu_texture_create_opt((dev), (Mel_Gpu_Texture_Opt){ __VA_ARGS__ })

void mel_gpu_texture_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex);
bool mel_gpu_texture_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex);

typedef enum
{
    MEL_GPU_TEXTURE_VIEW_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_TEXTURE_VIEW_CREATE_VK_FAILED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_TEXTURE_VIEW_CREATE_BAD_TEXTURE = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    // gpu-rhi.md §6.7: the view's bindless slot (= its slotmap index, §3.1) exceeds the heap class capacity.
    // Surfaced loudly so the caller branches (e.g. evict, or request a larger heap) rather than sampling an
    // unbound slot (CRITICAL-1 / MEL-ENGINE-VIII). Only on a bindless device whose live shader-readable view
    // count crosses a class cap.
    MEL_GPU_TEXTURE_VIEW_CREATE_BINDLESS_SLOT_EXHAUSTED = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Texture_View_Create_Status;

typedef struct
{
    Mel_Gpu_Texture           texture;
    Mel_Gpu_View_Dimension    dimension;
    Mel_Gpu_Format            format;
    Mel_Gpu_Subresource_Range range;
    const char*               name;
} Mel_Gpu_Texture_View_Opt;

typedef struct
{
    Mel_Gpu_Texture_View               value;
    Mel_Gpu_Texture_View_Create_Status status;
} Mel_Gpu_Texture_View_Create_Result;

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_view_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View_Opt opt);
#define mel_gpu_texture_view_create(dev, ...) mel_gpu_texture_view_create_opt((dev), (Mel_Gpu_Texture_View_Opt){ __VA_ARGS__ })

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_default_view(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex);

void mel_gpu_texture_view_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view);
bool mel_gpu_texture_view_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view);

typedef struct
{
    Mel_Gpu_Subresource_Range subresource;
    Mel_Gpu_Extent3           offset;
    Mel_Gpu_Extent3           extent;
    u32                       row_pitch;
    u32                       slice_pitch;
} Mel_Gpu_Texture_Region;

void mel_gpu_texture_write(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Region region, const void* data, usize bytes);
