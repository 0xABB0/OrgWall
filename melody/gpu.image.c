#define VK_NO_PROTOTYPES
#include "gpu.image.h"
#include "allocator.heap.h"

void mel_gpu_image_init_opt(Mel_Gpu_Image* img, Mel_Gpu_Device* dev, Mel_Gpu_Image_Opt opt)
{
    assert(img != nullptr);
    assert(dev != nullptr);
    assert(opt.width > 0);
    assert(opt.height > 0);
    assert(opt.format != VK_FORMAT_UNDEFINED);

    *img = (Mel_Gpu_Image){0};

    u32 mips = opt.mip_levels > 0 ? opt.mip_levels : 1;
    u32 layers = opt.layer_count > 0 ? opt.layer_count : 1;
    VkImageUsageFlags usage = opt.usage ? opt.usage : (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    VkImageAspectFlags aspect = opt.aspect ? opt.aspect : VK_IMAGE_ASPECT_COLOR_BIT;
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = opt.format,
        .extent = { opt.width, opt.height, 1 },
        .mipLevels = mips,
        .arrayLayers = layers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };

    VkResult r = vmaCreateImage(dev->vma, &image_info, &alloc_info, &img->image, &img->allocation, nullptr);
    assert(r == VK_SUCCESS);

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = img->image,
        .viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
        .format = opt.format,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = mips,
            .baseArrayLayer = 0,
            .layerCount = layers,
        },
    };

    r = vkCreateImageView(dev->device, &view_info, nullptr, &img->view);
    assert(r == VK_SUCCESS);

    img->format = opt.format;
    img->width = opt.width;
    img->height = opt.height;
    img->mip_levels = mips;
    img->layer_count = layers;
    img->aspect = aspect;
    img->alloc = alloc;

    u32 state_count = mips * layers;
    img->subresource_states = mel_alloc(alloc, sizeof(Mel_Gpu_Image_State) * state_count);
    for (u32 i = 0; i < state_count; i++)
    {
        img->subresource_states[i] = (Mel_Gpu_Image_State){
            .layout = VK_IMAGE_LAYOUT_UNDEFINED,
            .stage = VK_PIPELINE_STAGE_2_NONE,
            .access = VK_ACCESS_2_NONE,
        };
    }
}

void mel_gpu_image_shutdown(Mel_Gpu_Image* img, Mel_Gpu_Device* dev)
{
    assert(img != nullptr);
    assert(dev != nullptr);

    if (img->subresource_states)
    {
        mel_dealloc(img->alloc, img->subresource_states);
        img->subresource_states = nullptr;
    }

    if (img->view)
    {
        vkDestroyImageView(dev->device, img->view, nullptr);
        img->view = VK_NULL_HANDLE;
    }

    if (img->image && img->allocation)
    {
        vmaDestroyImage(dev->vma, img->image, img->allocation);
        img->image = VK_NULL_HANDLE;
        img->allocation = VK_NULL_HANDLE;
    }
}

VkPipelineStageFlags2 mel_gpu_image_layout_stage(VkImageLayout layout)
{
    switch (layout)
    {
        case VK_IMAGE_LAYOUT_UNDEFINED:                     return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:          return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:          return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:      return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:      return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:               return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        default:                                             return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
}

VkAccessFlags2 mel_gpu_image_layout_access(VkImageLayout layout)
{
    switch (layout)
    {
        case VK_IMAGE_LAYOUT_UNDEFINED:                     return VK_ACCESS_2_NONE;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:          return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:          return VK_ACCESS_2_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:      return VK_ACCESS_2_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:      return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:               return VK_ACCESS_2_NONE;
        default:                                             return VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
}

void mel_gpu_image_transition_subresource(Mel_Gpu_Image* img, VkCommandBuffer cmd,
                                          u32 mip, u32 layer, VkImageLayout new_layout)
{
    assert(img != nullptr);
    assert(mip < img->mip_levels);
    assert(layer < img->layer_count);

    u32 idx = mip * img->layer_count + layer;
    Mel_Gpu_Image_State* state = &img->subresource_states[idx];

    if (state->layout == new_layout) return;

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = state->stage ? state->stage : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask = state->access,
        .dstStageMask = mel_gpu_image_layout_stage(new_layout),
        .dstAccessMask = mel_gpu_image_layout_access(new_layout),
        .oldLayout = state->layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img->image,
        .subresourceRange = {
            .aspectMask = img->aspect,
            .baseMipLevel = mip,
            .levelCount = 1,
            .baseArrayLayer = layer,
            .layerCount = 1,
        },
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmd, &dep);

    state->layout = new_layout;
    state->stage = barrier.dstStageMask;
    state->access = barrier.dstAccessMask;
}

void mel_gpu_image_transition(Mel_Gpu_Image* img, VkCommandBuffer cmd,
                              VkImageLayout new_layout)
{
    assert(img != nullptr);

    Mel_Gpu_Image_State* state = &img->subresource_states[0];

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = state->stage ? state->stage : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask = state->access,
        .dstStageMask = mel_gpu_image_layout_stage(new_layout),
        .dstAccessMask = mel_gpu_image_layout_access(new_layout),
        .oldLayout = state->layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img->image,
        .subresourceRange = {
            .aspectMask = img->aspect,
            .baseMipLevel = 0,
            .levelCount = img->mip_levels,
            .baseArrayLayer = 0,
            .layerCount = img->layer_count,
        },
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmd, &dep);

    u32 total = img->mip_levels * img->layer_count;
    for (u32 i = 0; i < total; i++)
    {
        img->subresource_states[i].layout = new_layout;
        img->subresource_states[i].stage = barrier.dstStageMask;
        img->subresource_states[i].access = barrier.dstAccessMask;
    }
}

Mel_Gpu_Image_State mel_gpu_image_state(Mel_Gpu_Image* img, u32 mip, u32 layer)
{
    assert(img != nullptr);
    assert(mip < img->mip_levels);
    assert(layer < img->layer_count);
    return img->subresource_states[mip * img->layer_count + layer];
}
