#include "gpu.image.h"
#include "gpu.device.vulkan.h"
#include "gpu.cmd.h"
#include "gpu.types.vulkan.h"
#include "allocator.heap.h"

void mel_gpu_image_init_opt(Mel_Gpu_Image* img, Mel_Gpu_Device* dev, Mel_Gpu_Image_Opt opt)
{
    assert(img != nullptr);
    assert(dev != nullptr);
    assert(opt.width > 0);
    assert(opt.height > 0);
    assert(opt.format != MEL_GPU_FORMAT_UNDEFINED);

    *img = (Mel_Gpu_Image){0};

    u32 mips = opt.mip_levels > 0 ? opt.mip_levels : 1;
    u32 layers = opt.layer_count > 0 ? opt.layer_count : 1;
    VkImageUsageFlags usage = opt.usage
        ? mel__gpu_image_usage_to_vk(opt.usage)
        : (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    VkImageAspectFlags aspect = opt.aspect
        ? mel__gpu_aspect_to_vk(opt.aspect)
        : VK_IMAGE_ASPECT_COLOR_BIT;
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = mel__gpu_format_to_vk(opt.format),
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

    VkImage vk_image = VK_NULL_HANDLE;
    VmaAllocation vma_alloc = VK_NULL_HANDLE;
    VkResult r = vmaCreateImage(mel__gpu_device_vk(dev)->vma, &image_info, &alloc_info, &vk_image, &vma_alloc, nullptr);
    assert(r == VK_SUCCESS);

    VkImageView vk_view = VK_NULL_HANDLE;
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk_image,
        .viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
        .format = mel__gpu_format_to_vk(opt.format),
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = mips,
            .baseArrayLayer = 0,
            .layerCount = layers,
        },
    };

    r = vkCreateImageView(mel__gpu_device_vk(dev)->device, &view_info, nullptr, &vk_view);
    assert(r == VK_SUCCESS);

    img->_handle = vk_image;
    img->_view = vk_view;
    img->_allocation = vma_alloc;
    img->format = opt.format;
    img->width = opt.width;
    img->height = opt.height;
    img->mip_levels = mips;
    img->layer_count = layers;
    img->aspect = opt.aspect ? opt.aspect : MEL_GPU_ASPECT_COLOR;
    img->alloc = alloc;

    u32 state_count = mips * layers;
    img->subresource_states = mel_alloc(alloc, sizeof(Mel_Gpu_Image_State) * state_count);
    for (u32 i = 0; i < state_count; i++)
    {
        img->subresource_states[i] = (Mel_Gpu_Image_State){
            .layout = MEL_GPU_IMAGE_LAYOUT_UNDEFINED,
            .stage = MEL_GPU_STAGE_NONE,
            .access = MEL_GPU_ACCESS_NONE,
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

    if (img->_view)
    {
        vkDestroyImageView(mel__gpu_device_vk(dev)->device, (VkImageView)img->_view, nullptr);
        img->_view = nullptr;
    }

    if (img->_handle && img->_allocation)
    {
        vmaDestroyImage(mel__gpu_device_vk(dev)->vma, (VkImage)img->_handle, (VmaAllocation)img->_allocation);
        img->_handle = nullptr;
        img->_allocation = nullptr;
    }
}

Mel_Gpu_Stage mel_gpu_image_layout_stage(Mel_Gpu_Image_Layout layout)
{
    switch (layout)
    {
        case MEL_GPU_IMAGE_LAYOUT_UNDEFINED:                return MEL_GPU_STAGE_TOP_OF_PIPE;
        case MEL_GPU_IMAGE_LAYOUT_TRANSFER_DST:             return MEL_GPU_STAGE_TRANSFER;
        case MEL_GPU_IMAGE_LAYOUT_TRANSFER_SRC:             return MEL_GPU_STAGE_TRANSFER;
        case MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY:         return MEL_GPU_STAGE_FRAGMENT_SHADER;
        case MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT:         return MEL_GPU_STAGE_COLOR_ATTACHMENT_OUTPUT;
        case MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT: return MEL_GPU_STAGE_EARLY_FRAGMENT_TESTS;
        case MEL_GPU_IMAGE_LAYOUT_PRESENT:                  return MEL_GPU_STAGE_BOTTOM_OF_PIPE;
        default:                                            return MEL_GPU_STAGE_ALL_COMMANDS;
    }
}

Mel_Gpu_Access mel_gpu_image_layout_access(Mel_Gpu_Image_Layout layout)
{
    switch (layout)
    {
        case MEL_GPU_IMAGE_LAYOUT_UNDEFINED:                return MEL_GPU_ACCESS_NONE;
        case MEL_GPU_IMAGE_LAYOUT_TRANSFER_DST:             return MEL_GPU_ACCESS_TRANSFER_WRITE;
        case MEL_GPU_IMAGE_LAYOUT_TRANSFER_SRC:             return MEL_GPU_ACCESS_TRANSFER_READ;
        case MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY:         return MEL_GPU_ACCESS_SHADER_READ;
        case MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT:         return MEL_GPU_ACCESS_COLOR_ATTACHMENT_WRITE;
        case MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT: return MEL_GPU_ACCESS_DEPTH_STENCIL_WRITE;
        case MEL_GPU_IMAGE_LAYOUT_PRESENT:                  return MEL_GPU_ACCESS_NONE;
        default:                                            return MEL_GPU_ACCESS_MEMORY_READ | MEL_GPU_ACCESS_MEMORY_WRITE;
    }
}

void mel_gpu_image_transition_subresource(Mel_Gpu_Image* img, Mel_Gpu_Cmd* cmd,
                                          u32 mip, u32 layer, Mel_Gpu_Image_Layout new_layout)
{
    assert(img != nullptr);
    assert(cmd != nullptr);
    assert(mip < img->mip_levels);
    assert(layer < img->layer_count);

    u32 idx = mip * img->layer_count + layer;
    Mel_Gpu_Image_State* state = &img->subresource_states[idx];

    if (state->layout == new_layout) return;

    Mel_Gpu_Stage dst_stage = mel_gpu_image_layout_stage(new_layout);
    Mel_Gpu_Access dst_access = mel_gpu_image_layout_access(new_layout);

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = state->stage ? mel__gpu_stage_to_vk(state->stage) : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask = mel__gpu_access_to_vk(state->access),
        .dstStageMask = mel__gpu_stage_to_vk(dst_stage),
        .dstAccessMask = mel__gpu_access_to_vk(dst_access),
        .oldLayout = mel__gpu_image_layout_to_vk(state->layout),
        .newLayout = mel__gpu_image_layout_to_vk(new_layout),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = (VkImage)img->_handle,
        .subresourceRange = {
            .aspectMask = mel__gpu_aspect_to_vk(img->aspect),
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

    vkCmdPipelineBarrier2((VkCommandBuffer)cmd->_cmd, &dep);

    state->layout = new_layout;
    state->stage = dst_stage;
    state->access = dst_access;
}

void mel_gpu_image_transition(Mel_Gpu_Image* img, Mel_Gpu_Cmd* cmd,
                              Mel_Gpu_Image_Layout new_layout)
{
    assert(img != nullptr);
    assert(cmd != nullptr);

    Mel_Gpu_Image_State* state = &img->subresource_states[0];
    Mel_Gpu_Stage dst_stage = mel_gpu_image_layout_stage(new_layout);
    Mel_Gpu_Access dst_access = mel_gpu_image_layout_access(new_layout);

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = state->stage ? mel__gpu_stage_to_vk(state->stage) : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask = mel__gpu_access_to_vk(state->access),
        .dstStageMask = mel__gpu_stage_to_vk(dst_stage),
        .dstAccessMask = mel__gpu_access_to_vk(dst_access),
        .oldLayout = mel__gpu_image_layout_to_vk(state->layout),
        .newLayout = mel__gpu_image_layout_to_vk(new_layout),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = (VkImage)img->_handle,
        .subresourceRange = {
            .aspectMask = mel__gpu_aspect_to_vk(img->aspect),
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

    vkCmdPipelineBarrier2((VkCommandBuffer)cmd->_cmd, &dep);

    u32 total = img->mip_levels * img->layer_count;
    for (u32 i = 0; i < total; i++)
    {
        img->subresource_states[i].layout = new_layout;
        img->subresource_states[i].stage = dst_stage;
        img->subresource_states[i].access = dst_access;
    }
}

Mel_Gpu_Image_State mel_gpu_image_state(Mel_Gpu_Image* img, u32 mip, u32 layer)
{
    assert(img != nullptr);
    assert(mip < img->mip_levels);
    assert(layer < img->layer_count);
    return img->subresource_states[mip * img->layer_count + layer];
}
