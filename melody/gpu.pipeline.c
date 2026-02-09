#define VK_NO_PROTOTYPES
#include "gpu.pipeline.h"
#include "gpu.shader.h"
#include <SDL3/SDL_log.h>

static VkCullModeFlags cull_mode_to_vk(u32 mode)
{
    switch (mode)
    {
        case MEL_GPU_CULL_BACK:  return VK_CULL_MODE_BACK_BIT;
        case MEL_GPU_CULL_FRONT: return VK_CULL_MODE_FRONT_BIT;
        default:                 return VK_CULL_MODE_NONE;
    }
}

static VkPrimitiveTopology topology_to_vk(u32 topo)
{
    switch (topo)
    {
        case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case MEL_GPU_TOPOLOGY_LINE_LIST:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case MEL_GPU_TOPOLOGY_POINT_LIST:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:                              return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static void setup_blend(VkPipelineColorBlendAttachmentState* blend, u32 mode)
{
    blend->colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    if (mode == MEL_GPU_BLEND_NONE)
    {
        blend->blendEnable = VK_FALSE;
        return;
    }

    blend->blendEnable = VK_TRUE;

    if (mode == MEL_GPU_BLEND_ALPHA)
    {
        blend->srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend->dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend->colorBlendOp = VK_BLEND_OP_ADD;
        blend->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend->alphaBlendOp = VK_BLEND_OP_ADD;
    }
    else if (mode == MEL_GPU_BLEND_ADD)
    {
        blend->srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend->dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend->colorBlendOp = VK_BLEND_OP_ADD;
        blend->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend->alphaBlendOp = VK_BLEND_OP_ADD;
    }
    else if (mode == MEL_GPU_BLEND_MULTIPLY)
    {
        blend->srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        blend->dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend->colorBlendOp = VK_BLEND_OP_ADD;
        blend->srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        blend->dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend->alphaBlendOp = VK_BLEND_OP_ADD;
    }
}

void mel_gpu_pipeline_init_opt(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    assert(pipeline != nullptr);
    assert(dev != nullptr);
    assert(opt.shader != nullptr);
    assert(opt.color_format != VK_FORMAT_UNDEFINED);

    *pipeline = (Mel_Gpu_Pipeline){0};

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

        VkResult r = vkCreateDescriptorSetLayout(dev->device, &layout_info, nullptr, &pipeline->descriptor_layout);
        assert(r == VK_SUCCESS);

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

        r = vkCreateDescriptorPool(dev->device, &pool_info, nullptr, &pipeline->descriptor_pool);
        assert(r == VK_SUCCESS);
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

    VkResult r = vkCreatePipelineLayout(dev->device, &pipe_layout_info, nullptr, &pipeline->layout);
    assert(r == VK_SUCCESS);

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
        .topology = topology_to_vk(opt.topology),
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cull_mode_to_vk(opt.cull_mode),
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {0};
    setup_blend(&blend_attachment, opt.blend_mode);

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = opt.depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = opt.depth_write ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
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

    VkPipelineRenderingCreateInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &opt.color_format,
        .depthAttachmentFormat = opt.depth_format,
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
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = pipeline->layout,
    };

    r = vkCreateGraphicsPipelines(dev->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline);
    assert(r == VK_SUCCESS);

    SDL_Log("Pipeline created successfully");
}

void mel_gpu_pipeline_shutdown(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev)
{
    assert(pipeline != nullptr);
    assert(dev != nullptr);

    if (pipeline->pipeline) { vkDestroyPipeline(dev->device, pipeline->pipeline, nullptr); pipeline->pipeline = VK_NULL_HANDLE; }
    if (pipeline->layout) { vkDestroyPipelineLayout(dev->device, pipeline->layout, nullptr); pipeline->layout = VK_NULL_HANDLE; }
    if (pipeline->descriptor_pool) { vkDestroyDescriptorPool(dev->device, pipeline->descriptor_pool, nullptr); pipeline->descriptor_pool = VK_NULL_HANDLE; }
    if (pipeline->descriptor_layout) { vkDestroyDescriptorSetLayout(dev->device, pipeline->descriptor_layout, nullptr); pipeline->descriptor_layout = VK_NULL_HANDLE; }
}

void mel_gpu_pipeline_bind(Mel_Gpu_Pipeline* pipeline, VkCommandBuffer cmd)
{
    assert(pipeline != nullptr);
    assert(pipeline->pipeline != VK_NULL_HANDLE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

VkDescriptorSet mel_gpu_pipeline_alloc_descriptor(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev)
{
    assert(pipeline != nullptr);
    assert(dev != nullptr);
    assert(pipeline->descriptor_pool != VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pipeline->descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &pipeline->descriptor_layout,
    };

    VkDescriptorSet set;
    VkResult r = vkAllocateDescriptorSets(dev->device, &alloc_info, &set);
    assert(r == VK_SUCCESS);

    return set;
}

void mel_gpu_pipeline_write_texture(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev,
                                    VkDescriptorSet set, VkImageView view, VkSampler sampler)
{
    assert(pipeline != nullptr);
    assert(dev != nullptr);

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

    vkUpdateDescriptorSets(dev->device, 1, &write, 0, nullptr);
}
