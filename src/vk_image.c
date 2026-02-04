#define VK_NO_PROTOTYPES
#include "vk_image.h"

bool mel_vk_image_init_opt(Mel_VkImage* img, Mel_VkContext* ctx, Mel_VkImage_Opt opt)
{
    assert(img != nullptr);
    assert(ctx != nullptr);
    assert(opt.width > 0);
    assert(opt.height > 0);

    *img = (Mel_VkImage){0};

    if (opt.format == 0) opt.format = VK_FORMAT_R8G8B8A8_SRGB;
    if (opt.usage == 0) opt.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (opt.aspect == 0) opt.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = opt.format,
        .extent = { opt.width, opt.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = opt.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };

    VkResult result = vmaCreateImage(ctx->vma, &image_info, &alloc_info, &img->image, &img->allocation, nullptr);
    
    if (result != VK_SUCCESS)
    {
        return false;
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = img->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = opt.format,
        .subresourceRange = {
            .aspectMask = opt.aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    result = vkCreateImageView(ctx->device, &view_info, nullptr, &img->view);
    if (result != VK_SUCCESS)
    {
        vmaDestroyImage(ctx->vma, img->image, img->allocation);
        return false;
    }

    img->format = opt.format;
    img->width = opt.width;
    img->height = opt.height;

    return true;
}

void mel_vk_image_shutdown(Mel_VkImage* img, Mel_VkContext* ctx)
{
    assert(img != nullptr);
    assert(ctx != nullptr);

    if (img->view)
    {
        vkDestroyImageView(ctx->device, img->view, nullptr);
        img->view = VK_NULL_HANDLE;
    }

    if (img->image)
    {
        vmaDestroyImage(ctx->vma, img->image, img->allocation);
        img->image = VK_NULL_HANDLE;
        img->allocation = VK_NULL_HANDLE;
    }
}

void mel_vk_image_transition(Mel_VkImage* img, VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout)
{
    assert(img != nullptr);

    VkAccessFlags src_access = 0;
    VkAccessFlags dst_access = 0;
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        dst_access = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img->image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
