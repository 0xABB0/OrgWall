#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

// Classic descriptor-set path (gpu-rhi.md §6.7), the P2 peer of the device bindless heap — not a degraded
// fallback. A user who wants app-owned descriptor sets declares bind-group layouts, builds a non-bindless
// pipeline layout from them (pipeline_create.set_layouts), allocates bind groups, writes resources into
// them, and binds via mel_gpu_cmd_bind_descriptor_set. Because reflection now tells a runtime-array set 0
// (the heap signature) apart from a sized/app-owned set 0, a classic set-0 shader is no longer force-marked
// bindless (the A2 separation).
MEL_GPU_HANDLE(Mel_Gpu_Bind_Group_Layout);
MEL_GPU_HANDLE(Mel_Gpu_Bind_Group);

// Descriptor kinds are a closed protocol mapping onto VkDescriptorType / D3D12 descriptor ranges / WebGPU
// binding types; same MEL-CODE-001 carve-out as the format / state / sampler enums in this module.
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
    u32                     count; // array size at this binding; 0 is treated as 1
} Mel_Gpu_Bind_Group_Layout_Entry;

// A bind-group layout = one VkDescriptorSetLayout. The entries are copied; the layout may be destroyed once
// every pipeline and bind group built from it exists (Vulkan retains what it needs — caller contract).
Mel_Gpu_Bind_Group_Layout mel_gpu_bind_group_layout_create(Mel_Gpu_Device* dev, const Mel_Gpu_Bind_Group_Layout_Entry* entries, u32 count);
void                       mel_gpu_bind_group_layout_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout);
bool                       mel_gpu_bind_group_layout_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout);

// A bind group = one VkDescriptorSet allocated from the device's classic descriptor pool chain.
Mel_Gpu_Bind_Group mel_gpu_bind_group_create(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout);
void               mel_gpu_bind_group_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group);
bool               mel_gpu_bind_group_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group);

// Writes — `array_element` selects within an arrayed binding (0 for a single descriptor). Writing a binding
// an in-flight submission reads is the caller's contract, exactly as for the heap (gpu-rhi.md §6.7).
void mel_gpu_bind_group_write_texture(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view);
void mel_gpu_bind_group_write_sampler(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Sampler sampler);
void mel_gpu_bind_group_write_combined(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view, Mel_Gpu_Sampler sampler);
void mel_gpu_bind_group_write_buffer(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Buffer buffer);

// Bind a bind group at a set index for subsequent draws/dispatches (gpu-rhi.md §6.7). The bound pipeline's
// layout must have been created with this group's layout at that set index.
void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Command_List* cmd, u32 set_index, Mel_Gpu_Bind_Group group);
