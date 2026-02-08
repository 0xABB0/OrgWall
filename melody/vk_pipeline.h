#ifndef MEL_VK_PIPELINE_H
#define MEL_VK_PIPELINE_H

#include "vk_context.h"
#include "vk_shader.h"
#include "vk_swapchain.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
} Mel_VkPipeline;

typedef struct
{
    Mel_VkShader* shader;
    Mel_VkSwapchain* swapchain;
    VkVertexInputBindingDescription* bindings;
    u32 binding_count;
    VkVertexInputAttributeDescription* attributes;
    u32 attribute_count;
    u32 push_constant_size;
    bool use_texture;
    u32 max_descriptor_sets;
} Mel_VkPipeline_Opt;

bool mel_vk_pipeline_init_opt(Mel_VkPipeline* pipeline, Mel_VkContext* ctx, Mel_VkPipeline_Opt opt);
#define mel_vk_pipeline_init(pipeline, ctx, ...) mel_vk_pipeline_init_opt((pipeline), (ctx), (Mel_VkPipeline_Opt){__VA_ARGS__})

void mel_vk_pipeline_shutdown(Mel_VkPipeline* pipeline, Mel_VkContext* ctx);

void mel_vk_pipeline_bind(Mel_VkPipeline* pipeline, VkCommandBuffer cmd);

VkDescriptorSet mel_vk_pipeline_alloc_descriptor(Mel_VkPipeline* pipeline, Mel_VkContext* ctx);
void mel_vk_pipeline_write_texture(Mel_VkPipeline* pipeline, Mel_VkContext* ctx, VkDescriptorSet set, VkImageView view, VkSampler sampler);

#ifdef __cplusplus
}
#endif

#endif
