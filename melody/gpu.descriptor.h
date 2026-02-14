#pragma once

#include "gpu.device.h"

#define MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER  0
#define MEL_GPU_DESCRIPTOR_STORAGE_BUFFER  1
#define MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE   2
#define MEL_GPU_DESCRIPTOR_STORAGE_IMAGE   3
#define MEL_GPU_DESCRIPTOR_SAMPLER         4
#define MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER 5

typedef struct Mel_Gpu_Descriptor_Binding Mel_Gpu_Descriptor_Binding;
typedef struct Mel_Gpu_Descriptor_Layout Mel_Gpu_Descriptor_Layout;
typedef struct Mel_Gpu_Descriptor_Pool Mel_Gpu_Descriptor_Pool;

struct Mel_Gpu_Descriptor_Binding {
    u32 binding;
    u32 type;
    u32 count;
    VkShaderStageFlags stages;
};

struct Mel_Gpu_Descriptor_Layout {
    VkDescriptorSetLayout layout;
    u32 binding_count;
};

struct Mel_Gpu_Descriptor_Pool {
    VkDescriptorPool pool;
    VkDescriptorSetLayout layout;
    u32 max_sets;
};

typedef struct {
    Mel_Gpu_Descriptor_Binding* bindings;
    u32 binding_count;
} Mel_Gpu_Descriptor_Layout_Opt;

void mel_gpu_descriptor_layout_init_opt(Mel_Gpu_Descriptor_Layout* dl, Mel_Gpu_Device* dev, Mel_Gpu_Descriptor_Layout_Opt opt);
#define mel_gpu_descriptor_layout_init(dl, dev, ...) mel_gpu_descriptor_layout_init_opt((dl), (dev), (Mel_Gpu_Descriptor_Layout_Opt){__VA_ARGS__})

void mel_gpu_descriptor_layout_shutdown(Mel_Gpu_Descriptor_Layout* dl, Mel_Gpu_Device* dev);

typedef struct {
    Mel_Gpu_Descriptor_Layout* layout;
    u32 max_sets;
} Mel_Gpu_Descriptor_Pool_Opt;

void mel_gpu_descriptor_pool_init_opt(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev, Mel_Gpu_Descriptor_Pool_Opt opt);
#define mel_gpu_descriptor_pool_init(dp, dev, ...) mel_gpu_descriptor_pool_init_opt((dp), (dev), (Mel_Gpu_Descriptor_Pool_Opt){__VA_ARGS__})

void mel_gpu_descriptor_pool_shutdown(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev);

VkDescriptorSet mel_gpu_descriptor_pool_alloc(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev);

void mel_gpu_descriptor_write_texture(Mel_Gpu_Device* dev, VkDescriptorSet set,
                                      u32 binding, VkImageView view, VkSampler sampler);

void mel_gpu_descriptor_write_buffer(Mel_Gpu_Device* dev, VkDescriptorSet set,
                                     u32 binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range,
                                     VkDescriptorType type);

void mel_gpu_descriptor_bind(VkCommandBuffer cmd, VkPipelineLayout layout, VkDescriptorSet set);
