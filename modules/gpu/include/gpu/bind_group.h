#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

MEL_GPU_HANDLE(Mel_Gpu_Bind_Group_Layout);
MEL_GPU_HANDLE(Mel_Gpu_Bind_Group);

typedef enum
{
    MEL_GPU_DESCRIPTOR_SAMPLER = 0,
    MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE,
    MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
    MEL_GPU_DESCRIPTOR_STORAGE_IMAGE,
    MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER,
    MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
} Mel_Gpu_Descriptor_Kind;

typedef struct
{
    u32                     binding;
    Mel_Gpu_Descriptor_Kind kind;
    u32                     count;
} Mel_Gpu_Bind_Group_Layout_Entry;

Mel_Gpu_Bind_Group_Layout mel_gpu_bind_group_layout_create(Mel_Gpu_Device* dev, const Mel_Gpu_Bind_Group_Layout_Entry* entries, u32 count);
void                       mel_gpu_bind_group_layout_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout);
bool                       mel_gpu_bind_group_layout_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout);

Mel_Gpu_Bind_Group mel_gpu_bind_group_create(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout);
void               mel_gpu_bind_group_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group);
bool               mel_gpu_bind_group_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group);

void mel_gpu_bind_group_write_texture(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view);
void mel_gpu_bind_group_write_sampler(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Sampler sampler);
void mel_gpu_bind_group_write_combined(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view, Mel_Gpu_Sampler sampler);
void mel_gpu_bind_group_write_buffer(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Buffer buffer);

void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Command_List* cmd, u32 set_index, Mel_Gpu_Bind_Group group);
