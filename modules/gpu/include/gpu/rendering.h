#pragma once

#include <core/types.h>
#include <gpu/format.h>
#include <gpu/texture.h>

typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

typedef enum
{
    MEL_GPU_LOAD_CLEAR = 0,
    MEL_GPU_LOAD_LOAD,
    MEL_GPU_LOAD_DONT_CARE,
} Mel_Gpu_Load_Op;

typedef enum
{
    MEL_GPU_STORE_STORE = 0,
    MEL_GPU_STORE_DONT_CARE,
} Mel_Gpu_Store_Op;

typedef struct
{
    Mel_Gpu_Texture_View view;
    Mel_Gpu_Load_Op      load;
    Mel_Gpu_Store_Op     store;
    Mel_Gpu_Color        clear;
    Mel_Gpu_Texture_View resolve_view;
} Mel_Gpu_Color_Attachment;

typedef struct
{
    Mel_Gpu_Texture_View view;
    Mel_Gpu_Load_Op      load;
    Mel_Gpu_Store_Op     store;
    f32                  clear_depth;
    u32                  clear_stencil;
} Mel_Gpu_Depth_Attachment;

typedef struct
{
    const Mel_Gpu_Color_Attachment* colors;
    u32                             color_count;
    const Mel_Gpu_Depth_Attachment* depth;
    u32                             width;
    u32                             height;
} Mel_Gpu_Rendering_Opt;

void mel_gpu_cmd_begin_rendering_opt(Mel_Gpu_Command_List* cmd, Mel_Gpu_Rendering_Opt opt);
#define mel_gpu_cmd_begin_rendering(cmd, ...) mel_gpu_cmd_begin_rendering_opt((cmd), (Mel_Gpu_Rendering_Opt){ __VA_ARGS__ })

void mel_gpu_cmd_end_rendering(Mel_Gpu_Command_List* cmd);
