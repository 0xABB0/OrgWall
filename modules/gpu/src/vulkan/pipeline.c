#include "vulkan_backend.h"

#define MEL_GPU_VK_MAX_ATTRS 16

// A render pass compatible with the swapchain's (same single color format,
// 1 sample). Used only to create the pipeline; a pipeline works with any
// render-pass compatible with the one it was built against, so this is created
// and destroyed locally. swapchain.c builds an identical pass for rendering.
VkRenderPass mel_gpu__vk_make_render_pass(VkDevice device, VkFormat format)
{
    VkAttachmentDescription color = {
        .format         = format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference ref = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &ref,
    };
    VkSubpassDependency dep = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo rpci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &color,
        .subpassCount    = 1,
        .pSubpasses      = &sub,
        .dependencyCount = 1,
        .pDependencies   = &dep,
    };
    VkRenderPass rp;
    if (vkCreateRenderPass(device, &rpci, NULL, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
    return rp;
}

Mel_Gpu_Pipeline* mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    if (!dev || !opt.shader) return NULL;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT,   .module = opt.shader->vs, .pName = opt.shader->vertex_entry },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = opt.shader->fs, .pName = opt.shader->fragment_entry },
    };

    u32 attr_count = opt.vertex_layout_count;
    if (attr_count > MEL_GPU_VK_MAX_ATTRS) attr_count = MEL_GPU_VK_MAX_ATTRS;
    VkVertexInputAttributeDescription attrs[MEL_GPU_VK_MAX_ATTRS] = {0};
    for (u32 i = 0; i < attr_count; i++) {
        attrs[i].location = opt.vertex_layout[i].location;
        attrs[i].binding  = 0;
        attrs[i].format   = mel_gpu__vk_vertex_format(opt.vertex_layout[i].format);
        attrs[i].offset   = opt.vertex_layout[i].offset;
    }
    VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = opt.vertex_stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkPipelineVertexInputStateCreateInfo vin = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = opt.vertex_stride > 0 ? 1 : 0,
        .pVertexBindingDescriptions      = opt.vertex_stride > 0 ? &binding : NULL,
        .vertexAttributeDescriptionCount = attr_count,
        .pVertexAttributeDescriptions    = attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = mel_gpu__vk_topology(opt.topology),
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode    = mel_gpu__vk_cull(opt.cull),
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth   = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_att = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &blend_att,
    };
    VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dyn_states,
    };

    VkPipelineLayoutCreateInfo lci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(dev->device, &lci, NULL, &layout) != VK_SUCCESS) return NULL;

    VkRenderPass rp = mel_gpu__vk_make_render_pass(dev->device, mel_gpu__vk_color_format(opt.color_format));
    if (!rp) { vkDestroyPipelineLayout(dev->device, layout, NULL); return NULL; }

    VkGraphicsPipelineCreateInfo gci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = 2,
        .pStages             = stages,
        .pVertexInputState   = &vin,
        .pInputAssemblyState = &ia,
        .pViewportState      = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState   = &ms,
        .pColorBlendState    = &cb,
        .pDynamicState       = &dyn,
        .layout              = layout,
        .renderPass          = rp,
        .subpass             = 0,
    };
    VkPipeline pipeline;
    VkResult r = vkCreateGraphicsPipelines(dev->device, VK_NULL_HANDLE, 1, &gci, NULL, &pipeline);
    vkDestroyRenderPass(dev->device, rp, NULL);
    if (r != VK_SUCCESS) { vkDestroyPipelineLayout(dev->device, layout, NULL); return NULL; }

    Mel_Gpu_Pipeline* p = calloc(1, sizeof *p);
    if (!p) { vkDestroyPipeline(dev->device, pipeline, NULL); vkDestroyPipelineLayout(dev->device, layout, NULL); return NULL; }
    p->device   = dev;
    p->pipeline = pipeline;
    p->layout   = layout;
    return p;
}

void mel_gpu_pipeline_destroy(Mel_Gpu_Pipeline* pipe)
{
    if (!pipe) return;
    vkDeviceWaitIdle(pipe->device->device);
    vkDestroyPipeline(pipe->device->device, pipe->pipeline, NULL);
    vkDestroyPipelineLayout(pipe->device->device, pipe->layout, NULL);
    free(pipe);
}
