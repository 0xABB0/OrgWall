#include "vk_backend.h"

#include <gpu/pipeline.h>
#include <log/log.h>

#include <stdlib.h>

static VkPrimitiveTopology mel_gpu__topology(Mel_Gpu_Topology t)
{
    switch (t)
    {
    case MEL_GPU_TOPOLOGY_TRIANGLE_LIST:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case MEL_GPU_TOPOLOGY_LINE_LIST:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case MEL_GPU_TOPOLOGY_POINT_LIST:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

static VkCullModeFlags mel_gpu__cull(Mel_Gpu_Cull c)
{
    switch (c)
    {
    case MEL_GPU_CULL_NONE:
        return VK_CULL_MODE_NONE;
    case MEL_GPU_CULL_FRONT:
        return VK_CULL_MODE_FRONT_BIT;
    case MEL_GPU_CULL_BACK:
        return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    Mel_Gpu_Pipeline_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };

    VkShaderModule vs, fs;
    const char *   vs_entry, *fs_entry;
    if (!dev || !mel_gpu__shader_modules(dev, opt.shader, &vs, &fs, &vs_entry, &fs_entry))
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    // U12/U14: the shader is the source of truth for its layout. Reflection supplies the push-constant size
    // and bindless-set usage; the explicit opt fields override / augment them (the P2 manual path, §6.4).
    Mel_Gpu_Spirv_Reflection refl = { 0 };
    mel_gpu__shader_reflection(dev, opt.shader, &refl);
    bool bindless = opt.bindless || refl.uses_bindless_set;
    u32  pc_size = opt.push_constant_size ? opt.push_constant_size : refl.push_constant_size;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = vs_entry },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = fs_entry },
    };

    VkVertexInputBindingDescription binding = { .binding = 0, .stride = opt.vertex_stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };

    VkVertexInputAttributeDescription* attrs = NULL;
    if (opt.vertex_layout_count)
    {
        attrs = mel_alloc_array(dev->alloc, VkVertexInputAttributeDescription, opt.vertex_layout_count);
        for (u32 i = 0; i < opt.vertex_layout_count; i++)
            attrs[i] = (VkVertexInputAttributeDescription){
                .location = opt.vertex_layout[i].location,
                .binding = 0,
                .format = mel_gpu__vk_format(opt.vertex_layout[i].format),
                .offset = opt.vertex_layout[i].offset,
            };
    }

    VkPipelineVertexInputStateCreateInfo vin = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = opt.vertex_stride ? 1u : 0u,
        .pVertexBindingDescriptions = opt.vertex_stride ? &binding : NULL,
        .vertexAttributeDescriptionCount = opt.vertex_layout_count,
        .pVertexAttributeDescriptions = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = mel_gpu__topology(opt.topology) };

    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };

    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = mel_gpu__cull(opt.cull),
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cba };

    VkDynamicState                   dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn };

    // U14: a bindless pipeline puts the device heap at set 0 so the shader can index its resource arrays.
    // The two binding-model failures are kept distinct (gpu-rhi.md §6.7 / MEL-ENGINE-VIII).
    if (bindless && !dev->bindless.enabled)
    {
        mel_log_error("gpu", "pipeline_create '%s': bindless requested but the device has no bindless heap (MissingFeature)", opt.name ? opt.name : "(unnamed)");
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        return res;
    }

    // U11: bake immutable samplers into a dedicated descriptor set layout (gpu-rhi.md §6.3). The set sits
    // after the bindless set when both are present.
    VkDescriptorSetLayout static_sampler_layout = VK_NULL_HANDLE;
    if (opt.static_sampler_count)
    {
        VkSampler*                    immut = mel_alloc_array(dev->alloc, VkSampler, opt.static_sampler_count);
        VkDescriptorSetLayoutBinding* sbind = mel_alloc_array(dev->alloc, VkDescriptorSetLayoutBinding, opt.static_sampler_count);
        bool                          ok = true;
        for (u32 i = 0; i < opt.static_sampler_count; i++)
        {
            if (!mel_gpu__sampler_get(dev, opt.static_samplers[i].sampler, &immut[i]))
            {
                mel_log_error("gpu", "pipeline_create '%s': static sampler %u is not a live sampler handle", opt.name ? opt.name : "(unnamed)", i);
                ok = false;
                break;
            }
            sbind[i] = (VkDescriptorSetLayoutBinding){
                .binding = opt.static_samplers[i].binding,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_ALL,
                .pImmutableSamplers = &immut[i],
            };
        }
        if (ok)
        {
            VkDescriptorSetLayoutCreateInfo slci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = opt.static_sampler_count, .pBindings = sbind };
            if (vkCreateDescriptorSetLayout(dev->vk, &slci, NULL, &static_sampler_layout) != VK_SUCCESS)
                ok = false;
        }
        mel_dealloc(dev->alloc, immut);
        mel_dealloc(dev->alloc, sbind);
        if (!ok)
        {
            if (attrs)
                mel_dealloc(dev->alloc, attrs);
            res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
            return res;
        }
    }

    VkDescriptorSetLayout set_layouts[2];
    u32                   set_count = 0;
    if (bindless)
        set_layouts[set_count++] = dev->bindless.set_layout;
    if (static_sampler_layout)
        set_layouts[set_count++] = static_sampler_layout;

    VkPushConstantRange        pcr = { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = pc_size };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = set_count,
        .pSetLayouts = set_count ? set_layouts : NULL,
        .pushConstantRangeCount = pc_size ? 1u : 0u,
        .pPushConstantRanges = pc_size ? &pcr : NULL,
    };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(dev->vk, &plci, NULL, &layout);

    bool has_depth = opt.depth_format != MEL_GPU_FORMAT_UNDEFINED;
    VkPipelineDepthStencilStateCreateInfo dss = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = has_depth,
        .depthWriteEnable = has_depth,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };

    // U16: dynamic-rendering pipelines carry their attachment formats directly; render-pass is the floor.
    VkFormat                         color_vk = mel_gpu__vk_format(opt.color_format);
    VkPipelineRenderingCreateInfoKHR pri = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_vk,
        .depthAttachmentFormat = has_depth ? mel_gpu__vk_format(opt.depth_format) : VK_FORMAT_UNDEFINED,
    };

    VkRenderPass rp = VK_NULL_HANDLE;
    if (!dev->dynamic_rendering)
        rp = mel_gpu__make_render_pass(dev, color_vk);

    VkGraphicsPipelineCreateInfo gci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = dev->dynamic_rendering ? (void*)&pri : NULL,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vin,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = has_depth ? &dss : NULL,
        .pColorBlendState = &cb,
        .pDynamicState = &ds,
        .layout = layout,
        .renderPass = rp,
        .subpass = 0,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult   r = vkCreateGraphicsPipelines(dev->vk, VK_NULL_HANDLE, 1, &gci, NULL, &pipeline);
    if (rp)
        vkDestroyRenderPass(dev->vk, rp, NULL);
    if (attrs)
        mel_dealloc(dev->alloc, attrs);

    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkCreateGraphicsPipelines failed: %s", mel_gpu__vk_result_str(r));
        vkDestroyPipelineLayout(dev->vk, layout, NULL);
        if (static_sampler_layout)
            vkDestroyDescriptorSetLayout(dev->vk, static_sampler_layout, NULL);
        res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Pipeline_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.pipeline = pipeline;
    obj.layout = layout;
    obj.static_sampler_layout = static_sampler_layout;
    obj.bindless = bindless;
    res.value.slot = mel_gpu__table_insert(dev, &dev->pipelines, &obj);
    return res;
}

void mel_gpu_pipeline_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe)
{
    Mel_Gpu_Pipeline_Obj* o = mel_gpu__table_get(dev, &dev->pipelines, pipe.slot);
    if (!o)
        return;
    VkPipeline            p = o->pipeline;
    VkPipelineLayout      l = o->layout;
    VkDescriptorSetLayout sl = o->static_sampler_layout;
    mel_gpu__table_remove(dev, &dev->pipelines, pipe.slot);
    // U3 future-gated retirement: an in-flight command buffer may still reference this pipeline.
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .pipeline = p, .pipeline_layout = l, .descriptor_set_layout = sl });
}

bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe) { return mel_gpu__table_get(dev, &dev->pipelines, pipe.slot) != NULL; }

bool mel_gpu__pipeline_get(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, VkPipeline* out_pipe, VkPipelineLayout* out_layout)
{
    Mel_Gpu_Pipeline_Obj* o = mel_gpu__table_get(dev, &dev->pipelines, pipe.slot);
    if (!o)
        return false;
    *out_pipe = o->pipeline;
    *out_layout = o->layout;
    return true;
}

Mel_Gpu_Pipeline_Obj* mel_gpu__pipeline_obj(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe) { return mel_gpu__table_get(dev, &dev->pipelines, pipe.slot); }
