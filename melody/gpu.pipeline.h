#pragma once

#include "gpu.device.h"
#include "gpu.shader.fwd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEL_GPU_BLEND_NONE      0
#define MEL_GPU_BLEND_ALPHA     1
#define MEL_GPU_BLEND_ADD       2
#define MEL_GPU_BLEND_MULTIPLY  3

#define MEL_GPU_CULL_NONE       0
#define MEL_GPU_CULL_BACK       1
#define MEL_GPU_CULL_FRONT      2

#define MEL_GPU_TOPOLOGY_TRIANGLE_LIST  0
#define MEL_GPU_TOPOLOGY_TRIANGLE_STRIP 1
#define MEL_GPU_TOPOLOGY_LINE_LIST      2
#define MEL_GPU_TOPOLOGY_POINT_LIST     3

typedef struct Mel_Gpu_Pipeline Mel_Gpu_Pipeline;

struct Mel_Gpu_Pipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorPool* descriptor_pools;
    u32 descriptor_pool_count;
    u32 descriptor_pool_capacity;
    u32 descriptor_pool_max_sets;
    u64 hash;
};

typedef struct {
    Mel_Gpu_Shader* shader;
    VkVertexInputBindingDescription* bindings;
    u32 binding_count;
    VkVertexInputAttributeDescription* attributes;
    u32 attribute_count;
    VkFormat color_format;
    VkFormat depth_format;
    u32 blend_mode;
    u32 cull_mode;
    u32 topology;
    bool depth_test;
    bool depth_write;
    u32 push_constant_size;
    bool use_texture;
    u32 max_descriptor_sets;
} Mel_Gpu_Pipeline_Opt;

void mel_gpu_pipeline_init_opt(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt);
#define mel_gpu_pipeline_init(pipeline, dev, ...) mel_gpu_pipeline_init_opt((pipeline), (dev), (Mel_Gpu_Pipeline_Opt){__VA_ARGS__})

void mel_gpu_pipeline_shutdown(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev);

void mel_gpu_pipeline_bind(Mel_Gpu_Pipeline* pipeline, VkCommandBuffer cmd);

VkDescriptorSet mel_gpu_pipeline_alloc_descriptor(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev);
void mel_gpu_pipeline_write_texture(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev,
                                    VkDescriptorSet set, VkImageView view, VkSampler sampler);

#ifdef __cplusplus
}
#endif
