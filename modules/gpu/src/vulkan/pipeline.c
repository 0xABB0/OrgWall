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

static VkBlendFactor mel_gpu__vk_blend_factor(Mel_Gpu_Blend_Factor f)
{
    switch (f)
    {
    case MEL_GPU_BLEND_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case MEL_GPU_BLEND_ONE:
        return VK_BLEND_FACTOR_ONE;
    case MEL_GPU_BLEND_SRC_COLOR:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case MEL_GPU_BLEND_ONE_MINUS_SRC_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case MEL_GPU_BLEND_DST_COLOR:
        return VK_BLEND_FACTOR_DST_COLOR;
    case MEL_GPU_BLEND_ONE_MINUS_DST_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case MEL_GPU_BLEND_SRC_ALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case MEL_GPU_BLEND_DST_ALPHA:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case MEL_GPU_BLEND_ONE_MINUS_DST_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case MEL_GPU_BLEND_CONSTANT_COLOR:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case MEL_GPU_BLEND_ONE_MINUS_CONSTANT_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case MEL_GPU_BLEND_CONSTANT_ALPHA:
        return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case MEL_GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    case MEL_GPU_BLEND_SRC_ALPHA_SATURATE:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    }
    return VK_BLEND_FACTOR_ZERO;
}

static VkBlendOp mel_gpu__vk_blend_op(Mel_Gpu_Blend_Op o)
{
    switch (o)
    {
    case MEL_GPU_BLEND_OP_ADD:
        return VK_BLEND_OP_ADD;
    case MEL_GPU_BLEND_OP_SUBTRACT:
        return VK_BLEND_OP_SUBTRACT;
    case MEL_GPU_BLEND_OP_REVERSE_SUBTRACT:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case MEL_GPU_BLEND_OP_MIN:
        return VK_BLEND_OP_MIN;
    case MEL_GPU_BLEND_OP_MAX:
        return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

static VkStencilOp mel_gpu__vk_stencil_op(Mel_Gpu_Stencil_Op o)
{
    switch (o)
    {
    case MEL_GPU_STENCIL_KEEP:
        return VK_STENCIL_OP_KEEP;
    case MEL_GPU_STENCIL_ZERO:
        return VK_STENCIL_OP_ZERO;
    case MEL_GPU_STENCIL_REPLACE:
        return VK_STENCIL_OP_REPLACE;
    case MEL_GPU_STENCIL_INCREMENT_CLAMP:
        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case MEL_GPU_STENCIL_DECREMENT_CLAMP:
        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case MEL_GPU_STENCIL_INVERT:
        return VK_STENCIL_OP_INVERT;
    case MEL_GPU_STENCIL_INCREMENT_WRAP:
        return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case MEL_GPU_STENCIL_DECREMENT_WRAP:
        return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    return VK_STENCIL_OP_KEEP;
}

static VkStencilOpState mel_gpu__stencil_face(Mel_Gpu_Stencil_Face f)
{
    return (VkStencilOpState){
        .failOp = mel_gpu__vk_stencil_op(f.fail),
        .passOp = mel_gpu__vk_stencil_op(f.pass),
        .depthFailOp = mel_gpu__vk_stencil_op(f.depth_fail),
        .compareOp = mel_gpu__vk_compare_op(f.compare),
        .compareMask = f.compare_mask,
        .writeMask = f.write_mask,
        .reference = f.reference,
    };
}

static VkSampleCountFlagBits mel_gpu__sample_bits(u32 n)
{
    switch (n)
    {
    case 0:
    case 1:
        return VK_SAMPLE_COUNT_1_BIT;
    case 2:
        return VK_SAMPLE_COUNT_2_BIT;
    case 4:
        return VK_SAMPLE_COUNT_4_BIT;
    case 8:
        return VK_SAMPLE_COUNT_8_BIT;
    case 16:
        return VK_SAMPLE_COUNT_16_BIT;
    case 32:
        return VK_SAMPLE_COUNT_32_BIT;
    case 64:
        return VK_SAMPLE_COUNT_64_BIT;
    }
    return 0;
}

static VkRenderPass mel_gpu__make_pipeline_compat_render_pass(Mel_Gpu_Device* dev, const VkFormat* colors, u32 color_count, VkSampleCountFlagBits samples, VkFormat depth)
{
    bool                     has_depth = depth != VK_FORMAT_UNDEFINED;
    u32                      att_count = color_count + (has_depth ? 1u : 0u);
    VkAttachmentDescription* atts = att_count ? mel_alloc_array(dev->alloc, VkAttachmentDescription, att_count) : NULL;
    VkAttachmentReference*   color_refs = color_count ? mel_alloc_array(dev->alloc, VkAttachmentReference, color_count) : NULL;
    for (u32 i = 0; i < color_count; i++)
    {
        atts[i] = (VkAttachmentDescription){
            .format = colors[i],
            .samples = samples,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        color_refs[i] = (VkAttachmentReference){ .attachment = i, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    }
    VkAttachmentReference depth_ref = { .attachment = color_count, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    if (has_depth)
        atts[color_count] = (VkAttachmentDescription){
            .format = depth,
            .samples = samples,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = color_count,
        .pColorAttachments = color_refs,
        .pDepthStencilAttachment = has_depth ? &depth_ref : NULL,
    };
    VkRenderPassCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = att_count, .pAttachments = atts, .subpassCount = 1, .pSubpasses = &sub };
    VkRenderPass           rp = VK_NULL_HANDLE;
    vkCreateRenderPass(dev->vk, &ci, NULL, &rp);
    if (atts)
        mel_dealloc(dev->alloc, atts);
    if (color_refs)
        mel_dealloc(dev->alloc, color_refs);
    return rp;
}

static Mel_Gpu_Pipeline_Create_Status mel_gpu__binding_gate(Mel_Gpu_Device* dev, bool bindless, const Mel_Gpu_Spirv_Reflection* refl, const char* what, const char* dbg_name)
{
    if (bindless && !dev->bindless.enabled)
    {
        mel_log_error("gpu", "%s '%s': bindless requested but the device has no bindless heap (MissingFeature)", what, dbg_name);
        return MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
    }
    if (bindless)
        for (u32 s = 0; s < refl->set0_count; s++)
        {
            if (refl->set0[s].runtime_array)
                continue;
            u32 cap = mel_gpu__heap_cap_for_class(dev, refl->set0[s].binding);
            if (refl->set0[s].array_len > cap)
            {
                mel_log_error("gpu", "%s '%s': shader demands %u descriptors at set 0 binding %u but the heap holds %u (MissingBindlessSlot)", what, dbg_name, refl->set0[s].array_len, refl->set0[s].binding, cap);
                return MEL_GPU_PIPELINE_CREATE_MISSING_BINDLESS_SLOT;
            }
        }
    return MEL_GPU_PIPELINE_CREATE_OK;
}

static bool mel_gpu__build_spec_info(Mel_Gpu_Device* dev, const Mel_Gpu_Spec_Constant* spec_constants, u32 spec_constant_count, const Mel_Gpu_Spirv_Reflection* refl, bool warn_truncation, const char* what, const char* dbg_name,
                                     VkSpecializationMapEntry** out_entries, u32** out_data, VkSpecializationInfo* out_info)
{
    if (!spec_constant_count)
        return false;
    VkSpecializationMapEntry* entries = mel_alloc_array(dev->alloc, VkSpecializationMapEntry, spec_constant_count);
    u32*                      data = mel_alloc_array(dev->alloc, u32, spec_constant_count);
    for (u32 i = 0; i < spec_constant_count; i++)
    {
        bool declared = false;
        for (u32 j = 0; j < refl->spec_constant_count; j++)
            if (refl->spec_constants[j].id == spec_constants[i].id)
            {
                declared = true;
                if (warn_truncation && refl->spec_constants[j].bytes > 4)
                    mel_log_warn("gpu", "%s '%s': spec constant id %u is %u bytes; only the low 4 are baked", what, dbg_name, spec_constants[i].id, refl->spec_constants[j].bytes);
                break;
            }
        if (!declared)
            mel_log_warn("gpu", "%s '%s': spec constant id %u is not declared by the shader; ignored", what, dbg_name, spec_constants[i].id);
        data[i] = spec_constants[i].value;
        entries[i] = (VkSpecializationMapEntry){ .constantID = spec_constants[i].id, .offset = i * sizeof(u32), .size = sizeof(u32) };
    }
    *out_entries = entries;
    *out_data = data;
    *out_info = (VkSpecializationInfo){ .mapEntryCount = spec_constant_count, .pMapEntries = entries, .dataSize = (usize)spec_constant_count * sizeof(u32), .pData = data };
    return true;
}

static bool mel_gpu__build_pipeline_layout(Mel_Gpu_Device* dev, bool bindless, const Mel_Gpu_Bind_Group_Layout* set_layouts, u32 set_layout_count, VkDescriptorSetLayout static_sampler_layout, u32 pc_size, VkShaderStageFlags pc_stages, const char* what, const char* dbg_name, VkPipelineLayout* out_layout)
{
    *out_layout = VK_NULL_HANDLE;
    if (bindless && set_layout_count)
        mel_log_warn("gpu", "%s '%s': set_layouts ignored on a bindless pipeline (set 0 is the heap)", what, dbg_name);
    u32                    classic_count = bindless ? 0u : set_layout_count;
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
            if (!mel_gpu__bind_group_layout_vk(dev, set_layouts[i], &vkl))
            {
                mel_log_error("gpu", "%s '%s': set_layouts[%u] is not a live bind-group layout", what, dbg_name, i);
                sets_ok = false;
                break;
            }
            set_layouts_vk[set_count++] = vkl;
        }
    if (sets_ok && static_sampler_layout)
        set_layouts_vk[set_count++] = static_sampler_layout;

    if (sets_ok)
    {
        VkPushConstantRange        pcr = { .stageFlags = pc_stages, .offset = 0, .size = pc_size };
        VkPipelineLayoutCreateInfo plci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = set_count,
            .pSetLayouts = set_count ? set_layouts_vk : NULL,
            .pushConstantRangeCount = pc_size ? 1u : 0u,
            .pPushConstantRanges = pc_size ? &pcr : NULL,
        };
        vkCreatePipelineLayout(dev->vk, &plci, NULL, out_layout);
    }
    if (set_layouts_vk)
        mel_dealloc(dev->alloc, set_layouts_vk);
    return sets_ok;
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

    Mel_Gpu_Spirv_Reflection refl = { 0 };
    mel_gpu__shader_reflection(dev, opt.shader, &refl);
    bool        bindless = opt.bindless || refl.uses_bindless_set;
    u32         pc_size = opt.push_constant_size ? opt.push_constant_size : refl.push_constant_size;
    const char* dbg_name = opt.name ? opt.name : "(unnamed)";

    res.status = mel_gpu__binding_gate(dev, bindless, &refl, "pipeline_create", dbg_name);
    if (mel_gpu_failed(res.status))
        return res;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = vs_entry },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = fs_entry },
    };

    VkSpecializationMapEntry* spec_entries = NULL;
    u32*                      spec_data = NULL;
    VkSpecializationInfo      spec_info = { 0 };
    if (mel_gpu__build_spec_info(dev, opt.spec_constants, opt.spec_constant_count, &refl, true, "pipeline_create", dbg_name, &spec_entries, &spec_data, &spec_info))
    {
        stages[0].pSpecializationInfo = &spec_info;
        stages[1].pSpecializationInfo = &spec_info;
    }

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

    VkPolygonMode polygon = VK_POLYGON_MODE_FILL;
    if (opt.fill != MEL_GPU_FILL_SOLID)
    {
        if (dev->feat_fill_non_solid)
            polygon = opt.fill == MEL_GPU_FILL_WIREFRAME ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_POINT;
        else
            mel_log_warn("gpu", "pipeline_create '%s': non-solid fill requested but fill-mode-non-solid is not enabled; using solid", dbg_name);
    }
    if (opt.depth_bias && opt.depth_bias_clamp != 0.0f && !dev->feat_depth_bias_clamp)
        mel_log_warn("gpu", "pipeline_create '%s': depth-bias clamp requested but depth-bias-clamp is not enabled; clamp ignored", dbg_name);
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = polygon,
        .cullMode = mel_gpu__cull(opt.cull),
        .frontFace = opt.front_face == MEL_GPU_FRONT_FACE_CW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
        .depthBiasEnable = opt.depth_bias ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = opt.depth_bias_constant,
        .depthBiasClamp = dev->feat_depth_bias_clamp ? opt.depth_bias_clamp : 0.0f,
        .depthBiasSlopeFactor = opt.depth_bias_slope,
    };

    bool                  has_depth = opt.depth_format != MEL_GPU_FORMAT_UNDEFINED;
    u32                   req_samples = opt.samples ? opt.samples : 1;
    VkSampleCountFlagBits samples = mel_gpu__sample_bits(req_samples);
    if (req_samples > 1)
    {
        VkSampleCountFlags supported = dev->fb_color_samples;
        if (has_depth)
            supported &= dev->fb_depth_samples;
        if (samples == 0 || !(supported & samples))
        {
            mel_log_warn("gpu", "pipeline_create '%s': %u-sample MSAA is unsupported here; using 1", dbg_name, req_samples);
            samples = VK_SAMPLE_COUNT_1_BIT;
        }
    }
    if (samples == 0)
        samples = VK_SAMPLE_COUNT_1_BIT;
    if (opt.sample_shading && !dev->feat_sample_rate_shading)
        mel_log_warn("gpu", "pipeline_create '%s': sample shading requested but sample-rate-shading is not enabled; ignored", dbg_name);
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = samples,
        .sampleShadingEnable = (opt.sample_shading && dev->feat_sample_rate_shading) ? VK_TRUE : VK_FALSE,
        .minSampleShading = opt.min_sample_shading,
        .alphaToCoverageEnable = opt.alpha_to_coverage ? VK_TRUE : VK_FALSE,
    };

    VkDynamicState                   dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn };

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

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (!mel_gpu__build_pipeline_layout(dev, bindless, opt.set_layouts, opt.set_layout_count, static_sampler_layout, pc_size, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, "pipeline_create", dbg_name, &layout))
    {
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

    Mel_Gpu_Color_Target        single = { .format = opt.color_format, .blend = MEL_GPU_BLEND_OPAQUE };
    const Mel_Gpu_Color_Target* targets = opt.color_target_count ? opt.color_targets : (opt.color_format != MEL_GPU_FORMAT_UNDEFINED ? &single : NULL);
    u32                         target_count = opt.color_target_count ? opt.color_target_count : (opt.color_format != MEL_GPU_FORMAT_UNDEFINED ? 1u : 0u);

    VkFormat*                            color_formats = target_count ? mel_alloc_array(dev->alloc, VkFormat, target_count) : NULL;
    VkPipelineColorBlendAttachmentState* blend_atts = target_count ? mel_alloc_array(dev->alloc, VkPipelineColorBlendAttachmentState, target_count) : NULL;
    for (u32 i = 0; i < target_count; i++)
    {
        const Mel_Gpu_Blend* b = &targets[i].blend;
        color_formats[i] = mel_gpu__vk_format(targets[i].format);
        blend_atts[i] = (VkPipelineColorBlendAttachmentState){
            .blendEnable = b->enable ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = mel_gpu__vk_blend_factor(b->src_color),
            .dstColorBlendFactor = mel_gpu__vk_blend_factor(b->dst_color),
            .colorBlendOp = mel_gpu__vk_blend_op(b->color_op),
            .srcAlphaBlendFactor = mel_gpu__vk_blend_factor(b->src_alpha),
            .dstAlphaBlendFactor = mel_gpu__vk_blend_factor(b->dst_alpha),
            .alphaBlendOp = mel_gpu__vk_blend_op(b->alpha_op),
            .colorWriteMask = (VkColorComponentFlags)b->write_mask,
        };
    }
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = target_count,
        .pAttachments = blend_atts,
    };
    for (u32 i = 0; i < 4; i++)
        cb.blendConstants[i] = opt.blend_constants[i];

    bool                                  stencil_format = opt.depth_format == MEL_GPU_FORMAT_D24_UNORM_S8_UINT;
    VkPipelineDepthStencilStateCreateInfo dss = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    if (opt.depth_stencil)
    {
        const Mel_Gpu_Depth_Stencil* d = opt.depth_stencil;
        if (!has_depth)
            mel_log_warn("gpu", "pipeline_create '%s': depth_stencil supplied with no depth_format; ignored", dbg_name);
        VkCompareOp depth_op;
        if (d->depth_test && d->depth_compare == MEL_GPU_COMPARE_NONE)
        {
            mel_log_warn("gpu", "pipeline_create '%s': depth_test set with compare NONE; using LESS", dbg_name);
            depth_op = VK_COMPARE_OP_LESS;
        }
        else
            depth_op = mel_gpu__vk_compare_op(d->depth_compare);
        bool bounds = d->depth_bounds_test;
        if (bounds && !dev->feat_depth_bounds)
        {
            mel_log_warn("gpu", "pipeline_create '%s': depth-bounds test requested but depth-bounds is not enabled; ignored", dbg_name);
            bounds = false;
        }
        dss.depthTestEnable = d->depth_test ? VK_TRUE : VK_FALSE;
        dss.depthWriteEnable = d->depth_write ? VK_TRUE : VK_FALSE;
        dss.depthCompareOp = depth_op;
        dss.depthBoundsTestEnable = bounds ? VK_TRUE : VK_FALSE;
        dss.minDepthBounds = d->depth_bounds_min;
        dss.maxDepthBounds = d->depth_bounds_max;
        dss.stencilTestEnable = d->stencil_test ? VK_TRUE : VK_FALSE;
        dss.front = mel_gpu__stencil_face(d->front);
        dss.back = mel_gpu__stencil_face(d->back);
    }
    else
    {
        dss.depthTestEnable = has_depth ? VK_TRUE : VK_FALSE;
        dss.depthWriteEnable = has_depth ? VK_TRUE : VK_FALSE;
        dss.depthCompareOp = VK_COMPARE_OP_LESS;
    }

    VkPipelineRenderingCreateInfoKHR pri = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = target_count,
        .pColorAttachmentFormats = color_formats,
        .depthAttachmentFormat = has_depth ? mel_gpu__vk_format(opt.depth_format) : VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = stencil_format ? mel_gpu__vk_format(opt.depth_format) : VK_FORMAT_UNDEFINED,
    };

    VkRenderPass rp = VK_NULL_HANDLE;
    if (!dev->dynamic_rendering)
        rp = mel_gpu__make_pipeline_compat_render_pass(dev, color_formats, target_count, samples, has_depth ? mel_gpu__vk_format(opt.depth_format) : VK_FORMAT_UNDEFINED);

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
    if (spec_entries)
        mel_dealloc(dev->alloc, spec_entries);
    if (spec_data)
        mel_dealloc(dev->alloc, spec_data);
    if (color_formats)
        mel_dealloc(dev->alloc, color_formats);
    if (blend_atts)
        mel_dealloc(dev->alloc, blend_atts);

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

    res.status = mel_gpu__binding_gate(dev, bindless, &refl, "pipeline_compute_create", dbg_name);
    if (mel_gpu_failed(res.status))
        return res;

    VkSpecializationMapEntry* spec_entries = NULL;
    u32*                      spec_data = NULL;
    VkSpecializationInfo      spec_info = { 0 };
    bool                      has_spec = mel_gpu__build_spec_info(dev, opt.spec_constants, opt.spec_constant_count, &refl, false, "pipeline_compute_create", dbg_name, &spec_entries, &spec_data, &spec_info);

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline       pipeline = VK_NULL_HANDLE;
    VkResult         r = VK_ERROR_UNKNOWN;
    bool             sets_ok = mel_gpu__build_pipeline_layout(dev, bindless, opt.set_layouts, opt.set_layout_count, VK_NULL_HANDLE, pc_size, VK_SHADER_STAGE_COMPUTE_BIT, "pipeline_compute_create", dbg_name, &layout);
    if (sets_ok)
    {
        VkComputePipelineCreateInfo cci = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = cs, .pName = cs_entry, .pSpecializationInfo = has_spec ? &spec_info : NULL },
            .layout = layout,
        };
        r = vkCreateComputePipelines(dev->vk, VK_NULL_HANDLE, 1, &cci, NULL, &pipeline);
    }

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
    const void* trk = mel_gpu__track_key(&dev->pipelines, pipe.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    Mel_Gpu_Pipeline_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->pipelines, pipe.slot, &o))
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    VkPipeline            p = o.pipeline;
    VkPipelineLayout      l = o.layout;
    VkDescriptorSetLayout sl = o.static_sampler_layout;
    Mel_Gpu_Sampler*      ss = o.static_samplers;
    u32                   ssc = o.static_sampler_count;
    mel_gpu__table_remove(dev, &dev->pipelines, pipe.slot);
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .pipeline = p, .pipeline_layout = l, .descriptor_set_layout = sl });
    for (u32 i = 0; i < ssc; i++)
        mel_gpu_sampler_destroy(dev, ss[i]);
    if (ss)
        mel_dealloc(dev->alloc, ss);
    mel_gpu__track_exit(dev, trk);
}

bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe) { return mel_gpu__table_alive(dev, &dev->pipelines, pipe.slot); }

bool mel_gpu__pipeline_get(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, VkPipeline* out_pipe, VkPipelineLayout* out_layout)
{
    Mel_Gpu_Pipeline_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->pipelines, pipe.slot, &o))
        return false;
    *out_pipe = o.pipeline;
    *out_layout = o.layout;
    return true;
}

bool mel_gpu__pipeline_obj(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, Mel_Gpu_Pipeline_Obj* out) { return mel_gpu__table_get_copy(dev, &dev->pipelines, pipe.slot, out); }
