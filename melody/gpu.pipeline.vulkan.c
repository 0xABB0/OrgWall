#include "gpu.pipeline.h"
#include "gpu.device.vulkan.h"
#include "gpu.shader.h"
#include "gpu.cmd.h"
#include "gpu.types.vulkan.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "log.h"
#include <string.h>

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

static VkDescriptorType mel__pipeline_descriptor_type_to_vk(u32 type)
{
    switch (type)
    {
        case MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case MEL_GPU_DESCRIPTOR_STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case MEL_GPU_DESCRIPTOR_STORAGE_IMAGE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case MEL_GPU_DESCRIPTOR_SAMPLER: return VK_DESCRIPTOR_TYPE_SAMPLER;
        case MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default: assert(false && "Unknown descriptor type"); return VK_DESCRIPTOR_TYPE_MAX_ENUM;
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

static VkDescriptorPool mel__gpu_pipeline_create_descriptor_pool(Mel_Gpu_Device* dev,
    const Mel_Gpu_Descriptor_Binding* bindings, u32 binding_count, u32 max_sets)
{
    VkDescriptorPoolSize pool_sizes[16] = {0};
    u32 pool_size_count = 0;

    for (u32 i = 0; i < binding_count; i++)
    {
        VkDescriptorType type = mel__pipeline_descriptor_type_to_vk(bindings[i].type);
        bool found = false;
        for (u32 j = 0; j < pool_size_count; j++)
        {
            if (pool_sizes[j].type != type)
                continue;
            pool_sizes[j].descriptorCount += (bindings[i].count > 0 ? bindings[i].count : 1) * max_sets;
            found = true;
            break;
        }

        if (!found)
        {
            assert(pool_size_count < SDL_arraysize(pool_sizes));
            pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){
                .type = type,
                .descriptorCount = (bindings[i].count > 0 ? bindings[i].count : 1) * max_sets,
            };
        }
    }

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = max_sets,
        .poolSizeCount = pool_size_count,
        .pPoolSizes = pool_sizes,
    };

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult r = vkCreateDescriptorPool(mel__gpu_device_vk(dev)->device, &pool_info, nullptr, &pool);
    assert(r == VK_SUCCESS);
    return pool;
}

static void mel__gpu_pipeline_track_descriptor_pool(Mel_Gpu_Pipeline* pipeline, VkDescriptorPool pool)
{
    if (pipeline->descriptor_pool_count >= pipeline->descriptor_pool_capacity)
    {
        u32 new_capacity = pipeline->descriptor_pool_capacity == 0 ? 4 : pipeline->descriptor_pool_capacity * 2;
        usize size = sizeof(void*) * new_capacity;
        pipeline->_descriptor_pools = pipeline->_descriptor_pools
            ? mel_realloc(mel_alloc_heap(), pipeline->_descriptor_pools, size)
            : mel_alloc(mel_alloc_heap(), size);
        pipeline->descriptor_pool_capacity = new_capacity;
    }

    pipeline->_descriptor_pools[pipeline->descriptor_pool_count++] = pool;
    pipeline->_descriptor_pool = pool;
}

void mel_gpu_pipeline_init_opt(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    assert(pipeline != nullptr);
    assert(dev != nullptr);
    assert(opt.shader != nullptr);

    *pipeline = (Mel_Gpu_Pipeline){0};
    pipeline->_bind_point = opt.pipeline_type;

    Mel_Gpu_Descriptor_Binding default_texture_binding = {
        .binding = 0,
        .type = MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
        .count = 1,
        .stages = MEL_GPU_SHADER_STAGE_FRAGMENT,
    };
    Mel_Gpu_Descriptor_Binding* descriptor_bindings = opt.descriptor_bindings;
    u32 descriptor_binding_count = opt.descriptor_binding_count;
    if (descriptor_binding_count == 0 && opt.use_texture)
    {
        descriptor_bindings = &default_texture_binding;
        descriptor_binding_count = 1;
    }

    if (descriptor_binding_count > 0)
    {
        pipeline->descriptor_bindings = mel_alloc(mel_alloc_heap(),
            sizeof(Mel_Gpu_Descriptor_Binding) * descriptor_binding_count);
        pipeline->descriptor_binding_count = descriptor_binding_count;
        __builtin_memcpy(pipeline->descriptor_bindings, descriptor_bindings,
            sizeof(Mel_Gpu_Descriptor_Binding) * descriptor_binding_count);

        VkDescriptorSetLayoutBinding layout_bindings[16] = {0};
        assert(descriptor_binding_count <= SDL_arraysize(layout_bindings));
        for (u32 i = 0; i < descriptor_binding_count; i++)
        {
            layout_bindings[i] = (VkDescriptorSetLayoutBinding){
                .binding = descriptor_bindings[i].binding,
                .descriptorType = mel__pipeline_descriptor_type_to_vk(descriptor_bindings[i].type),
                .descriptorCount = descriptor_bindings[i].count > 0 ? descriptor_bindings[i].count : 1,
                .stageFlags = mel__gpu_shader_stage_to_vk(descriptor_bindings[i].stages),
            };
        }
        VkDescriptorSetLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = descriptor_binding_count,
            .pBindings = layout_bindings,
        };

        VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
        VkResult r = vkCreateDescriptorSetLayout(mel__gpu_device_vk(dev)->device, &layout_info, nullptr, &desc_layout);
        assert(r == VK_SUCCESS);
        pipeline->_descriptor_layout = desc_layout;

        pipeline->descriptor_pool_max_sets = opt.max_descriptor_sets > 0 ? opt.max_descriptor_sets : 16;
        mel__gpu_pipeline_track_descriptor_pool(pipeline,
            mel__gpu_pipeline_create_descriptor_pool(dev, pipeline->descriptor_bindings, pipeline->descriptor_binding_count,
                pipeline->descriptor_pool_max_sets));
    }

    VkPushConstantRange push_range = {
        .stageFlags = opt.push_constant_stages
            ? mel__gpu_shader_stage_to_vk(opt.push_constant_stages)
            : VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = opt.push_constant_size > 0 ? opt.push_constant_size : 64,
    };

    u32 total_set_count = (descriptor_binding_count > 0 ? 1 : 0) + opt.extra_set_layout_count;
    VkDescriptorSetLayout all_set_layouts[8];
    assert(total_set_count <= 8);

    u32 set_idx = 0;
    if (descriptor_binding_count > 0)
        all_set_layouts[set_idx++] = (VkDescriptorSetLayout)pipeline->_descriptor_layout;
    for (u32 i = 0; i < opt.extra_set_layout_count; i++)
        all_set_layouts[set_idx++] = (VkDescriptorSetLayout)opt.extra_set_layouts[i];

    VkPipelineLayoutCreateInfo pipe_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = total_set_count,
        .pSetLayouts = total_set_count > 0 ? all_set_layouts : nullptr,
        .pushConstantRangeCount = opt.push_constant_size > 0 ? 1 : 0,
        .pPushConstantRanges = opt.push_constant_size > 0 ? &push_range : nullptr,
    };

    VkPipelineLayout vk_layout = VK_NULL_HANDLE;
    VkResult r = vkCreatePipelineLayout(mel__gpu_device_vk(dev)->device, &pipe_layout_info, nullptr, &vk_layout);
    assert(r == VK_SUCCESS);
    pipeline->_layout = vk_layout;

    if (opt.pipeline_type == MEL_GPU_PIPELINE_COMPUTE)
    {
        assert(opt.shader->_compute != nullptr);
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = (VkShaderModule)opt.shader->_compute,
            .pName = "main",
        };
        VkComputePipelineCreateInfo pipeline_info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = (VkPipelineLayout)pipeline->_layout,
        };
        VkPipeline vk_pipeline = VK_NULL_HANDLE;
        VkResult r = vkCreateComputePipelines(mel__gpu_device_vk(dev)->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &vk_pipeline);
        assert(r == VK_SUCCESS);
        pipeline->_pipeline = vk_pipeline;
        mel_log_debug("gpu.pipeline", "Pipeline created successfully");
        return;
    }

    assert(opt.color_format != MEL_GPU_FORMAT_UNDEFINED ||
        (opt.color_formats != nullptr && opt.color_format_count > 0));

    VkPipelineShaderStageCreateInfo stages[3] = {0};
    u32 stage_count = 0;
    if (opt.pipeline_type == MEL_GPU_PIPELINE_MESH)
    {
        assert(opt.shader->_mesh != nullptr);
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
            .module = (VkShaderModule)opt.shader->_mesh,
            .pName = "main",
        };
    }
    else
    {
        assert(opt.shader->_vertex != nullptr);
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = (VkShaderModule)opt.shader->_vertex,
            .pName = "main",
        };
    }
    assert(opt.shader->_fragment != nullptr);
    stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = (VkShaderModule)opt.shader->_fragment,
        .pName = "main",
    };

    VkVertexInputBindingDescription vk_bindings[16] = {0};
    assert(opt.binding_count <= SDL_arraysize(vk_bindings));
    for (u32 i = 0; i < opt.binding_count; i++)
    {
        vk_bindings[i] = (VkVertexInputBindingDescription){
            .binding = opt.bindings[i].binding,
            .stride = opt.bindings[i].stride,
            .inputRate = opt.bindings[i].input_rate == 0
                ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE,
        };
    }

    VkVertexInputAttributeDescription vk_attributes[16] = {0};
    assert(opt.attribute_count <= SDL_arraysize(vk_attributes));
    for (u32 i = 0; i < opt.attribute_count; i++)
    {
        vk_attributes[i] = (VkVertexInputAttributeDescription){
            .location = opt.attributes[i].location,
            .binding = opt.attributes[i].binding,
            .format = mel__gpu_format_to_vk(opt.attributes[i].format),
            .offset = opt.attributes[i].offset,
        };
    }

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = opt.binding_count,
        .pVertexBindingDescriptions = vk_bindings,
        .vertexAttributeDescriptionCount = opt.attribute_count,
        .pVertexAttributeDescriptions = vk_attributes,
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
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState blend_attachments[8] = {0};
    u32 color_attachment_count = opt.color_format_count > 0 ? opt.color_format_count : 1;
    assert(color_attachment_count <= SDL_arraysize(blend_attachments));
    for (u32 i = 0; i < color_attachment_count; i++)
        setup_blend(&blend_attachments[i], opt.blend_mode);

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = color_attachment_count,
        .pAttachments = blend_attachments,
    };

    static const VkCompareOp s_compare_map[] = {
        [MEL_GPU_COMPARE_LESS]             = VK_COMPARE_OP_LESS,
        [MEL_GPU_COMPARE_NEVER]            = VK_COMPARE_OP_NEVER,
        [MEL_GPU_COMPARE_EQUAL]            = VK_COMPARE_OP_EQUAL,
        [MEL_GPU_COMPARE_LESS_OR_EQUAL]    = VK_COMPARE_OP_LESS_OR_EQUAL,
        [MEL_GPU_COMPARE_GREATER]          = VK_COMPARE_OP_GREATER,
        [MEL_GPU_COMPARE_NOT_EQUAL]        = VK_COMPARE_OP_NOT_EQUAL,
        [MEL_GPU_COMPARE_GREATER_OR_EQUAL] = VK_COMPARE_OP_GREATER_OR_EQUAL,
        [MEL_GPU_COMPARE_ALWAYS]           = VK_COMPARE_OP_ALWAYS,
    };
    assert(opt.depth_compare < 8);
    VkCompareOp vk_compare = s_compare_map[opt.depth_compare];

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = opt.depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = opt.depth_write ? VK_TRUE : VK_FALSE,
        .depthCompareOp = vk_compare,
    };

    VkDynamicState dynamic_states[4] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    u32 dynamic_state_count = 2;
    if (opt.dynamic_cull_mode)
        dynamic_states[dynamic_state_count++] = VK_DYNAMIC_STATE_CULL_MODE;

    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamic_state_count,
        .pDynamicStates = dynamic_states,
    };

    VkFormat color_formats_local[8] = {0};
    if (opt.color_format_count > 0)
    {
        for (u32 i = 0; i < opt.color_format_count; i++)
            color_formats_local[i] = mel__gpu_format_to_vk(opt.color_formats[i]);
    }
    else
    {
        color_formats_local[0] = mel__gpu_format_to_vk(opt.color_format);
    }

    VkPipelineRenderingCreateInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = color_attachment_count,
        .pColorAttachmentFormats = color_formats_local,
        .depthAttachmentFormat = mel__gpu_format_to_vk(opt.depth_format),
    };

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = stage_count,
        .pStages = stages,
        .pVertexInputState = opt.pipeline_type == MEL_GPU_PIPELINE_MESH ? nullptr : &vertex_input,
        .pInputAssemblyState = opt.pipeline_type == MEL_GPU_PIPELINE_MESH ? nullptr : &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = (VkPipelineLayout)pipeline->_layout,
    };

    VkPipeline vk_pipeline = VK_NULL_HANDLE;
    r = vkCreateGraphicsPipelines(mel__gpu_device_vk(dev)->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &vk_pipeline);
    assert(r == VK_SUCCESS);
    pipeline->_pipeline = vk_pipeline;

    mel_log_debug("gpu.pipeline", "Pipeline created successfully");
}

void mel_gpu_pipeline_shutdown(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev)
{
    assert(pipeline != nullptr);
    assert(dev != nullptr);

    if (pipeline->_pipeline) { vkDestroyPipeline(mel__gpu_device_vk(dev)->device, (VkPipeline)pipeline->_pipeline, nullptr); pipeline->_pipeline = nullptr; }
    if (pipeline->_layout) { vkDestroyPipelineLayout(mel__gpu_device_vk(dev)->device, (VkPipelineLayout)pipeline->_layout, nullptr); pipeline->_layout = nullptr; }
    for (u32 i = 0; i < pipeline->descriptor_pool_count; i++)
        if (pipeline->_descriptor_pools[i])
            vkDestroyDescriptorPool(mel__gpu_device_vk(dev)->device, (VkDescriptorPool)pipeline->_descriptor_pools[i], nullptr);
    if (pipeline->_descriptor_pools)
        mel_dealloc(mel_alloc_heap(), pipeline->_descriptor_pools);
    pipeline->_descriptor_pools = nullptr;
    pipeline->descriptor_pool_count = 0;
    pipeline->descriptor_pool_capacity = 0;
    pipeline->_descriptor_pool = nullptr;
    if (pipeline->descriptor_bindings)
        mel_dealloc(mel_alloc_heap(), pipeline->descriptor_bindings);
    pipeline->descriptor_bindings = nullptr;
    pipeline->descriptor_binding_count = 0;
    if (pipeline->_descriptor_layout) { vkDestroyDescriptorSetLayout(mel__gpu_device_vk(dev)->device, (VkDescriptorSetLayout)pipeline->_descriptor_layout, nullptr); pipeline->_descriptor_layout = nullptr; }
}

void mel_gpu_pipeline_bind(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Cmd* cmd)
{
    assert(pipeline != nullptr);
    assert(pipeline->_pipeline != nullptr);
    VkPipelineBindPoint bind_point;
    switch (pipeline->_bind_point) {
        case MEL_GPU_PIPELINE_COMPUTE: bind_point = VK_PIPELINE_BIND_POINT_COMPUTE; break;
        default: bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS; break;
    }
    vkCmdBindPipeline((VkCommandBuffer)cmd->_cmd, bind_point, (VkPipeline)pipeline->_pipeline);
}

void* mel_gpu_pipeline_alloc_descriptor(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev)
{
    assert(pipeline != nullptr);
    assert(dev != nullptr);
    assert(pipeline->_descriptor_pool != nullptr);

    VkDescriptorSetLayout vk_desc_layout = (VkDescriptorSetLayout)pipeline->_descriptor_layout;
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = (VkDescriptorPool)pipeline->_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk_desc_layout,
    };

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult r = vkAllocateDescriptorSets(mel__gpu_device_vk(dev)->device, &alloc_info, &set);
    if (r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL)
    {
        u32 next_max_sets = pipeline->descriptor_pool_max_sets > 0 ? pipeline->descriptor_pool_max_sets * 2 : 16;
        VkDescriptorPool new_pool = mel__gpu_pipeline_create_descriptor_pool(dev,
            pipeline->descriptor_bindings, pipeline->descriptor_binding_count, next_max_sets);
        mel__gpu_pipeline_track_descriptor_pool(pipeline, new_pool);
        pipeline->descriptor_pool_max_sets = next_max_sets;
        alloc_info.descriptorPool = (VkDescriptorPool)pipeline->_descriptor_pool;
        r = vkAllocateDescriptorSets(mel__gpu_device_vk(dev)->device, &alloc_info, &set);
    }
    assert(r == VK_SUCCESS);

    return set;
}

void mel_gpu_pipeline_write_texture(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev,
                                    void* set, void* view, void* sampler)
{
    mel_gpu_pipeline_write_texture_binding(pipeline, dev, set, 0, view, sampler);
}

void mel_gpu_pipeline_write_texture_binding(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev,
                                            void* set, u32 binding, void* view, void* sampler)
{
    MEL_UNUSED(pipeline);
    assert(dev != nullptr);
    mel_gpu_descriptor_write_texture(dev, set, binding, view, sampler);
}

void mel_gpu_pipeline_write_buffer_binding(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev,
                                           void* set, u32 binding, Mel_Gpu_Buffer* buffer,
                                           u64 offset, u64 range, u32 type)
{
    MEL_UNUSED(pipeline);
    assert(dev != nullptr);
    mel_gpu_descriptor_write_buffer(dev, set, binding, buffer, offset, range, type);
}
