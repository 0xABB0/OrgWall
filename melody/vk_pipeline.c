#define VK_NO_PROTOTYPES
#include "vk_pipeline.h"
#include <SDL3/SDL_log.h>

bool mel_vk_pipeline_init_opt(Mel_VkPipeline* pipeline, Mel_VkContext* ctx, Mel_VkPipeline_Opt opt)
{
    assert(pipeline != nullptr);
    assert(ctx != nullptr);
    assert(opt.shader != nullptr);
    assert(opt.swapchain != nullptr);

    *pipeline = (Mel_VkPipeline){0};

    if (opt.use_texture)
    {
        VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };

        VkDescriptorSetLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &binding,
        };

        VkResult result = vkCreateDescriptorSetLayout(ctx->device, &layout_info, nullptr, &pipeline->descriptor_layout);
        if (result != VK_SUCCESS) return false;

        u32 max_sets = opt.max_descriptor_sets > 0 ? opt.max_descriptor_sets : 16;

        VkDescriptorPoolSize pool_size = {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = max_sets,
        };

        VkDescriptorPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = max_sets,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size,
        };

        result = vkCreateDescriptorPool(ctx->device, &pool_info, nullptr, &pipeline->descriptor_pool);
        if (result != VK_SUCCESS)
        {
            vkDestroyDescriptorSetLayout(ctx->device, pipeline->descriptor_layout, nullptr);
            return false;
        }
    }

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = opt.push_constant_size > 0 ? opt.push_constant_size : 64,
    };

    VkPipelineLayoutCreateInfo pipe_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = opt.use_texture ? 1 : 0,
        .pSetLayouts = opt.use_texture ? &pipeline->descriptor_layout : nullptr,
        .pushConstantRangeCount = opt.push_constant_size > 0 ? 1 : 0,
        .pPushConstantRanges = opt.push_constant_size > 0 ? &push_range : nullptr,
    };

    VkResult result = vkCreatePipelineLayout(ctx->device, &pipe_layout_info, nullptr, &pipeline->layout);
    if (result != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = opt.shader->vertex,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = opt.shader->fragment,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = opt.binding_count,
        .pVertexBindingDescriptions = opt.bindings,
        .vertexAttributeDescriptionCount = opt.attribute_count,
        .pVertexAttributeDescriptions = opt.attributes,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (f32)opt.swapchain->extent.width,
        .height = (f32)opt.swapchain->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = opt.swapchain->extent,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    VkFormat color_format = opt.swapchain->format;

    VkPipelineRenderingCreateInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = pipeline->layout,
    };

    result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline);
    if (result != VK_SUCCESS)
    {
        SDL_Log("Failed to create graphics pipeline: %d", result);
        vkDestroyPipelineLayout(ctx->device, pipeline->layout, nullptr);
        return false;
    }

    SDL_Log("Pipeline created successfully");
    return true;
}

void mel_vk_pipeline_shutdown(Mel_VkPipeline* pipeline, Mel_VkContext* ctx)
{
    assert(pipeline != nullptr);
    assert(ctx != nullptr);

    if (pipeline->pipeline)
    {
        vkDestroyPipeline(ctx->device, pipeline->pipeline, nullptr);
        pipeline->pipeline = VK_NULL_HANDLE;
    }

    if (pipeline->layout)
    {
        vkDestroyPipelineLayout(ctx->device, pipeline->layout, nullptr);
        pipeline->layout = VK_NULL_HANDLE;
    }

    if (pipeline->descriptor_pool)
    {
        vkDestroyDescriptorPool(ctx->device, pipeline->descriptor_pool, nullptr);
        pipeline->descriptor_pool = VK_NULL_HANDLE;
    }

    if (pipeline->descriptor_layout)
    {
        vkDestroyDescriptorSetLayout(ctx->device, pipeline->descriptor_layout, nullptr);
        pipeline->descriptor_layout = VK_NULL_HANDLE;
    }
}

void mel_vk_pipeline_bind(Mel_VkPipeline* pipeline, VkCommandBuffer cmd)
{
    assert(pipeline != nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

VkDescriptorSet mel_vk_pipeline_alloc_descriptor(Mel_VkPipeline* pipeline, Mel_VkContext* ctx)
{
    assert(pipeline != nullptr);
    assert(ctx != nullptr);
    assert(pipeline->descriptor_pool != VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pipeline->descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &pipeline->descriptor_layout,
    };

    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(ctx->device, &alloc_info, &set);
    if (result != VK_SUCCESS)
    {
        SDL_Log("Failed to allocate descriptor set: %d", result);
        return VK_NULL_HANDLE;
    }

    return set;
}

void mel_vk_pipeline_write_texture(Mel_VkPipeline* pipeline, Mel_VkContext* ctx, VkDescriptorSet set, VkImageView view, VkSampler sampler)
{
    assert(pipeline != nullptr);
    assert(ctx != nullptr);

    VkDescriptorImageInfo image_info = {
        .sampler = sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };

    vkUpdateDescriptorSets(ctx->device, 1, &write, 0, nullptr);
}
