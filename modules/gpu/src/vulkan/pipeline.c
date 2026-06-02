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

// U14: the heap's per-class slot capacity, looked up by the set-0 binding index — which is the engine-
// canonical heap class index. A shader declaring a sized descriptor array longer than this at set 0 cannot
// be satisfied by the heap: that is MissingBindlessSlot, distinct from MissingFeature (gpu-rhi.md §6.7).
static u32 mel_gpu__heap_cap_for_binding(Mel_Gpu_Device* dev, u32 binding)
{
    switch (binding)
    {
    case MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE:
        return dev->bindless.cap_sampled_image;
    case MEL_GPU_BINDLESS_BINDING_SAMPLER:
        return dev->bindless.cap_sampler;
    case MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER:
        return dev->bindless.cap_storage_buffer;
    case MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER:
        return dev->bindless.cap_uniform_buffer;
    case MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE:
        return dev->bindless.cap_storage_image;
    default:
        return 0;
    }
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

    // U12/U14: the shader is the source of truth for its layout. Reflection supplies the push-constant size,
    // bindless-set usage, vertex input, and spec constants; the explicit opt fields override / augment them
    // (the P2 manual path, §6.4). `refl` aliases the shader's owned arrays — read-only, never freed here.
    Mel_Gpu_Spirv_Reflection refl = { 0 };
    mel_gpu__shader_reflection(dev, opt.shader, &refl);
    bool        bindless = opt.bindless || refl.uses_bindless_set;
    u32         pc_size = opt.push_constant_size ? opt.push_constant_size : refl.push_constant_size;
    const char* dbg_name = opt.name ? opt.name : "(unnamed)";

    // Binding-model gates fire before any allocation (gpu-rhi.md §6.7 / MEL-ENGINE-VIII): MissingFeature when
    // the shader/opt wants the heap and the device has none; MissingBindlessSlot when a bindless shader
    // demands more set-0 descriptors of a class than the heap holds. The two have different remedies — grow
    // the heap vs. request more capabilities — so they are never conflated.
    if (bindless && !dev->bindless.enabled)
    {
        mel_log_error("gpu", "pipeline_create '%s': bindless requested but the device has no bindless heap (MissingFeature)", dbg_name);
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        return res;
    }
    if (bindless)
        for (u32 s = 0; s < refl.set0_count; s++)
        {
            if (refl.set0[s].runtime_array) // an unbounded array is satisfied by the partially-bound heap
                continue;
            u32 cap = mel_gpu__heap_cap_for_binding(dev, refl.set0[s].binding);
            if (refl.set0[s].array_len > cap)
            {
                mel_log_error("gpu", "pipeline_create '%s': shader demands %u descriptors at set 0 binding %u but the heap holds %u (MissingBindlessSlot)", dbg_name, refl.set0[s].array_len, refl.set0[s].binding, cap);
                res.status = MEL_GPU_PIPELINE_CREATE_MISSING_BINDLESS_SLOT;
                return res;
            }
        }

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = vs_entry },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = fs_entry },
    };

    // U12: specialization constants baked at create. Reflection records the declared set so a supplied value
    // for an undeclared id warns (MEL-CODE-007) rather than silently no-opping; unknown entries Vulkan
    // ignores. One shared VkSpecializationInfo serves both stages (entries unused by a stage are ignored).
    VkSpecializationMapEntry* spec_entries = NULL;
    u32*                      spec_data = NULL;
    VkSpecializationInfo      spec_info = { 0 };
    if (opt.spec_constant_count)
    {
        spec_entries = mel_alloc_array(dev->alloc, VkSpecializationMapEntry, opt.spec_constant_count);
        spec_data = mel_alloc_array(dev->alloc, u32, opt.spec_constant_count);
        for (u32 i = 0; i < opt.spec_constant_count; i++)
        {
            bool declared = false;
            for (u32 j = 0; j < refl.spec_constant_count; j++)
                if (refl.spec_constants[j].id == opt.spec_constants[i].id)
                {
                    declared = true;
                    if (refl.spec_constants[j].bytes > 4)
                        mel_log_warn("gpu", "pipeline_create '%s': spec constant id %u is %u bytes; only the low 4 are baked", dbg_name, opt.spec_constants[i].id, refl.spec_constants[j].bytes);
                    break;
                }
            if (!declared)
                mel_log_warn("gpu", "pipeline_create '%s': spec constant id %u is not declared by the shader; ignored", dbg_name, opt.spec_constants[i].id);
            spec_data[i] = opt.spec_constants[i].value;
            spec_entries[i] = (VkSpecializationMapEntry){ .constantID = opt.spec_constants[i].id, .offset = i * sizeof(u32), .size = sizeof(u32) };
        }
        spec_info = (VkSpecializationInfo){ .mapEntryCount = opt.spec_constant_count, .pMapEntries = spec_entries, .dataSize = (usize)opt.spec_constant_count * sizeof(u32), .pData = spec_data };
        stages[0].pSpecializationInfo = &spec_info;
        stages[1].pSpecializationInfo = &spec_info;
    }

    // Vertex input: reflection-derived by default (single interleaved binding, tight-packed), the explicit
    // opt layout as the P2 override (gpu-rhi.md §6.5). A fullscreen-triangle vertex shader reflects no
    // attributes, so it falls through to no vertex input exactly as before.
    bool                          from_reflection = opt.vertex_layout_count == 0 && opt.vertex_stride == 0 && refl.vertex_attr_count > 0;
    u32                           layout_count = from_reflection ? refl.vertex_attr_count : opt.vertex_layout_count;
    u32                           stride = from_reflection ? refl.vertex_stride : opt.vertex_stride;
    VkVertexInputBindingDescription binding = { .binding = 0, .stride = stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };

    VkVertexInputAttributeDescription* attrs = NULL;
    if (layout_count)
    {
        attrs = mel_alloc_array(dev->alloc, VkVertexInputAttributeDescription, layout_count);
        for (u32 i = 0; i < layout_count; i++)
            attrs[i] = (VkVertexInputAttributeDescription){
                .location = from_reflection ? refl.vertex_attrs[i].location : opt.vertex_layout[i].location,
                .binding = 0,
                .format = mel_gpu__vk_format(from_reflection ? refl.vertex_attrs[i].format : opt.vertex_layout[i].format),
                .offset = from_reflection ? refl.vertex_attrs[i].offset : opt.vertex_layout[i].offset,
            };
    }

    VkPipelineVertexInputStateCreateInfo vin = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = stride ? 1u : 0u,
        .pVertexBindingDescriptions = stride ? &binding : NULL,
        .vertexAttributeDescriptionCount = layout_count,
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
            if (spec_entries)
                mel_dealloc(dev->alloc, spec_entries);
            if (spec_data)
                mel_dealloc(dev->alloc, spec_data);
            res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
            return res;
        }
    }

    // Compose the pipeline layout's descriptor sets. Bindless: set 0 = heap. Classic (§6.7 P2 peer): the
    // app-owned set layouts at sets 0..N-1 — mutually exclusive with the heap, since set 0 cannot be both.
    // A static-sampler set, when present, follows whichever path occupied the lower indices.
    if (bindless && opt.set_layout_count)
        mel_log_warn("gpu", "pipeline_create '%s': set_layouts ignored on a bindless pipeline (set 0 is the heap)", dbg_name);
    u32                    classic_count = bindless ? 0u : opt.set_layout_count;
    u32                    total_sets = (bindless ? 1u : classic_count) + (static_sampler_layout ? 1u : 0u);
    VkDescriptorSetLayout* set_layouts_vk = total_sets ? mel_alloc_array(dev->alloc, VkDescriptorSetLayout, total_sets) : NULL;
    u32                    set_count = 0;
    bool                   sets_ok = true;
    if (bindless)
        set_layouts_vk[set_count++] = dev->bindless.set_layout;
    else
        for (u32 i = 0; i < classic_count; i++)
        {
            VkDescriptorSetLayout vkl = VK_NULL_HANDLE;
            if (!mel_gpu__bind_group_layout_vk(dev, opt.set_layouts[i], &vkl))
            {
                mel_log_error("gpu", "pipeline_create '%s': set_layouts[%u] is not a live bind-group layout", dbg_name, i);
                sets_ok = false;
                break;
            }
            set_layouts_vk[set_count++] = vkl;
        }
    if (sets_ok && static_sampler_layout)
        set_layouts_vk[set_count++] = static_sampler_layout;

    if (!sets_ok)
    {
        if (set_layouts_vk)
            mel_dealloc(dev->alloc, set_layouts_vk);
        if (static_sampler_layout)
            vkDestroyDescriptorSetLayout(dev->vk, static_sampler_layout, NULL);
        if (attrs)
            mel_dealloc(dev->alloc, attrs);
        if (spec_entries)
            mel_dealloc(dev->alloc, spec_entries);
        if (spec_data)
            mel_dealloc(dev->alloc, spec_data);
        res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
        return res;
    }

    VkPushConstantRange        pcr = { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = pc_size };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = set_count,
        .pSetLayouts = set_count ? set_layouts_vk : NULL,
        .pushConstantRangeCount = pc_size ? 1u : 0u,
        .pPushConstantRanges = pc_size ? &pcr : NULL,
    };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(dev->vk, &plci, NULL, &layout);
    if (set_layouts_vk)
        mel_dealloc(dev->alloc, set_layouts_vk); // the pipeline layout retains what it needs

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
    if (spec_entries) // copied into the pipeline by vkCreateGraphicsPipelines; ours to release now
        mel_dealloc(dev->alloc, spec_entries);
    if (spec_data)
        mel_dealloc(dev->alloc, spec_data);

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
    obj.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    obj.pc_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    // U11/U13: take one lifetime claim per static sampler so it cannot be freed under a live pipeline.
    if (opt.static_sampler_count)
    {
        obj.static_samplers = mel_alloc_array(dev->alloc, Mel_Gpu_Sampler, opt.static_sampler_count);
        obj.static_sampler_count = opt.static_sampler_count;
        for (u32 i = 0; i < opt.static_sampler_count; i++)
        {
            obj.static_samplers[i] = opt.static_samplers[i].sampler;
            mel_gpu__sampler_retain(dev, opt.static_samplers[i].sampler);
        }
    }
    res.value.slot = mel_gpu__table_insert(dev, &dev->pipelines, &obj);
    return res;
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_compute_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Opt opt)
{
    Mel_Gpu_Pipeline_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };

    VkShaderModule cs;
    const char*    cs_entry;
    if (!dev || !mel_gpu__shader_compute_module(dev, opt.shader, &cs, &cs_entry))
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    Mel_Gpu_Spirv_Reflection refl = { 0 };
    mel_gpu__shader_reflection(dev, opt.shader, &refl);
    bool        bindless = opt.bindless || refl.uses_bindless_set;
    u32         pc_size = opt.push_constant_size ? opt.push_constant_size : refl.push_constant_size;
    const char* dbg_name = opt.name ? opt.name : "(unnamed)";

    // Same binding-model gates as graphics (gpu-rhi.md §6.7): MissingFeature without a heap, MissingBindlessSlot
    // for an over-cap set-0 demand. Fire before any allocation.
    if (bindless && !dev->bindless.enabled)
    {
        mel_log_error("gpu", "pipeline_compute_create '%s': bindless requested but the device has no bindless heap (MissingFeature)", dbg_name);
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        return res;
    }
    if (bindless)
        for (u32 s = 0; s < refl.set0_count; s++)
        {
            if (refl.set0[s].runtime_array)
                continue;
            u32 cap = mel_gpu__heap_cap_for_binding(dev, refl.set0[s].binding);
            if (refl.set0[s].array_len > cap)
            {
                mel_log_error("gpu", "pipeline_compute_create '%s': shader demands %u descriptors at set 0 binding %u but the heap holds %u (MissingBindlessSlot)", dbg_name, refl.set0[s].array_len, refl.set0[s].binding, cap);
                res.status = MEL_GPU_PIPELINE_CREATE_MISSING_BINDLESS_SLOT;
                return res;
            }
        }

    VkSpecializationMapEntry* spec_entries = NULL;
    u32*                      spec_data = NULL;
    VkSpecializationInfo      spec_info = { 0 };
    if (opt.spec_constant_count)
    {
        spec_entries = mel_alloc_array(dev->alloc, VkSpecializationMapEntry, opt.spec_constant_count);
        spec_data = mel_alloc_array(dev->alloc, u32, opt.spec_constant_count);
        for (u32 i = 0; i < opt.spec_constant_count; i++)
        {
            bool declared = false;
            for (u32 j = 0; j < refl.spec_constant_count; j++)
                if (refl.spec_constants[j].id == opt.spec_constants[i].id)
                {
                    declared = true;
                    break;
                }
            if (!declared)
                mel_log_warn("gpu", "pipeline_compute_create '%s': spec constant id %u is not declared by the shader; ignored", dbg_name, opt.spec_constants[i].id);
            spec_data[i] = opt.spec_constants[i].value;
            spec_entries[i] = (VkSpecializationMapEntry){ .constantID = opt.spec_constants[i].id, .offset = i * sizeof(u32), .size = sizeof(u32) };
        }
        spec_info = (VkSpecializationInfo){ .mapEntryCount = opt.spec_constant_count, .pMapEntries = spec_entries, .dataSize = (usize)opt.spec_constant_count * sizeof(u32), .pData = spec_data };
    }

    u32                    classic_count = bindless ? 0u : opt.set_layout_count;
    u32                    total_sets = bindless ? 1u : classic_count;
    VkDescriptorSetLayout* set_layouts_vk = total_sets ? mel_alloc_array(dev->alloc, VkDescriptorSetLayout, total_sets) : NULL;
    u32                    set_count = 0;
    bool                   sets_ok = true;
    if (bindless)
        set_layouts_vk[set_count++] = dev->bindless.set_layout;
    else
        for (u32 i = 0; i < classic_count; i++)
        {
            VkDescriptorSetLayout vkl = VK_NULL_HANDLE;
            if (!mel_gpu__bind_group_layout_vk(dev, opt.set_layouts[i], &vkl))
            {
                mel_log_error("gpu", "pipeline_compute_create '%s': set_layouts[%u] is not a live bind-group layout", dbg_name, i);
                sets_ok = false;
                break;
            }
            set_layouts_vk[set_count++] = vkl;
        }

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline       pipeline = VK_NULL_HANDLE;
    VkResult         r = VK_ERROR_UNKNOWN;
    if (sets_ok)
    {
        VkPushConstantRange        pcr = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = pc_size };
        VkPipelineLayoutCreateInfo plci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = set_count,
            .pSetLayouts = set_count ? set_layouts_vk : NULL,
            .pushConstantRangeCount = pc_size ? 1u : 0u,
            .pPushConstantRanges = pc_size ? &pcr : NULL,
        };
        vkCreatePipelineLayout(dev->vk, &plci, NULL, &layout);

        VkComputePipelineCreateInfo cci = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = cs, .pName = cs_entry, .pSpecializationInfo = opt.spec_constant_count ? &spec_info : NULL },
            .layout = layout,
        };
        r = vkCreateComputePipelines(dev->vk, VK_NULL_HANDLE, 1, &cci, NULL, &pipeline);
    }

    if (set_layouts_vk)
        mel_dealloc(dev->alloc, set_layouts_vk);
    if (spec_entries)
        mel_dealloc(dev->alloc, spec_entries);
    if (spec_data)
        mel_dealloc(dev->alloc, spec_data);

    if (!sets_ok)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
        return res;
    }
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkCreateComputePipelines failed: %s", mel_gpu__vk_result_str(r));
        if (layout)
            vkDestroyPipelineLayout(dev->vk, layout, NULL);
        res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Pipeline_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.pipeline = pipeline;
    obj.layout = layout;
    obj.bindless = bindless;
    obj.bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    obj.pc_stages = VK_SHADER_STAGE_COMPUTE_BIT;
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
    Mel_Gpu_Sampler*      ss = o->static_samplers;
    u32                   ssc = o->static_sampler_count;
    mel_gpu__table_remove(dev, &dev->pipelines, pipe.slot);
    // U3 future-gated retirement: an in-flight command buffer may still reference this pipeline.
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .pipeline = p, .pipeline_layout = l, .descriptor_set_layout = sl });
    // U11/U13: release the static-sampler lifetime claims after deferring the pipeline, so a sampler whose
    // last claim was this pipeline retires no earlier than the layout that baked it (gpu-rhi.md §6.3 / §3.3).
    for (u32 i = 0; i < ssc; i++)
        mel_gpu_sampler_destroy(dev, ss[i]);
    if (ss)
        mel_dealloc(dev->alloc, ss);
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
