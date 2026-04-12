#include "swapchain.image.h"
#include "swapchain.h"
#include "gpu.device.vulkan.h"
#include "gpu.cmd.h"
#include "gpu.buffer.h"
#include "gpu.format.h"
#include "gpu.types.vulkan.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "log.h"

typedef struct {
    const Mel_Alloc* alloc;
    VmaAllocation* image_allocs;
    Mel_Gpu_Image_Layout* image_layouts;

    Mel_Gpu_Buffer staging;
    bool has_staging;

    Mel_Swapchain_Image_Present_Fn on_present;
    void* user_data;

    u32 current_frame;
    u32 frame_count;
} Mel_Image_Swapchain;

static bool create_images(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Image_Swapchain* img = sc->data;
    const Mel_Alloc* alloc = img->alloc;

    sc->_images = mel_alloc(alloc, sizeof(void*) * sc->image_count);
    sc->_image_views = mel_alloc(alloc, sizeof(void*) * sc->image_count);
    img->image_allocs = mel_alloc(alloc, sizeof(VmaAllocation) * sc->image_count);
    img->image_layouts = mel_alloc(alloc, sizeof(Mel_Gpu_Image_Layout) * sc->image_count);

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    for (u32 i = 0; i < sc->image_count; i++)
    {
        img->image_layouts[i] = MEL_GPU_IMAGE_LAYOUT_UNDEFINED;
        VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = mel__gpu_format_to_vk(sc->format),
            .extent = { sc->extent_width, sc->extent_height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VmaAllocationCreateInfo vma_info = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        VkImage vk_image = VK_NULL_HANDLE;
        VkResult r = vmaCreateImage(mel__gpu_device_vk(dev)->vma, &image_info, &vma_info,
                                     &vk_image, &img->image_allocs[i], nullptr);
        if (r != VK_SUCCESS)
        {
            mel_log_error("gpu.swapchain", "Failed to create image swapchain image %u: %d", i, r);
            sc->image_count = i;
            return false;
        }
        sc->_images[i] = vk_image;

        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk_image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = mel__gpu_format_to_vk(sc->format),
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkImageView vk_view = VK_NULL_HANDLE;
        VkResult rv = vkCreateImageView(mel__gpu_device_vk(dev)->device, &view_info, nullptr, &vk_view);
        if (rv != VK_SUCCESS)
        {
            mel_log_error("gpu.swapchain", "Failed to create image swapchain view %u: %d", i, rv);
            vmaDestroyImage(mel__gpu_device_vk(dev)->vma, vk_image, img->image_allocs[i]);
            sc->image_count = i;
            return false;
        }
        sc->_image_views[i] = vk_view;
    }

    return true;
}

static void destroy_images(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Image_Swapchain* img = sc->data;
    const Mel_Alloc* alloc = img->alloc;

    if (sc->_image_views)
    {
        for (u32 i = 0; i < sc->image_count; i++)
            vkDestroyImageView(mel__gpu_device_vk(dev)->device, (VkImageView)sc->_image_views[i], nullptr);
        mel_dealloc(alloc, sc->_image_views);
        sc->_image_views = nullptr;
    }

    if (sc->_images && img->image_allocs)
    {
        for (u32 i = 0; i < sc->image_count; i++)
            vmaDestroyImage(mel__gpu_device_vk(dev)->vma, (VkImage)sc->_images[i], img->image_allocs[i]);
        mel_dealloc(alloc, sc->_images);
        mel_dealloc(alloc, img->image_allocs);
        sc->_images = nullptr;
        img->image_allocs = nullptr;
    }

    if (img->image_layouts)
    {
        mel_dealloc(alloc, img->image_layouts);
        img->image_layouts = nullptr;
    }
}

static bool image_acquire(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev;
    Mel_Image_Swapchain* img = sc->data;
    sc->current_image = img->current_frame;
    return true;
}

static void image_prepare_present(Mel_Swapchain* sc, Mel_Gpu_Cmd* cmd)
{
    Mel_Image_Swapchain* img = sc->data;
    if (!img->on_present)
        return;

    VkCommandBuffer vk_cmd = (VkCommandBuffer)cmd->_cmd;

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = (VkImage)sc->_images[sc->current_image],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(vk_cmd, &dep);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { sc->extent_width, sc->extent_height, 1 },
    };

    vkCmdCopyImageToBuffer(vk_cmd, (VkImage)sc->_images[sc->current_image],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           (VkBuffer)img->staging._handle, 1, &region);
}

static void image_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Image_Swapchain* img = sc->data;

    if (img->on_present)
    {
        vkDeviceWaitIdle(mel__gpu_device_vk(dev)->device);

        u32 pixel_size = mel_gpu_format_size(sc->format);
        u32 stride = sc->extent_width * pixel_size;

        img->on_present(img->staging.mapped, sc->extent_width, sc->extent_height, stride, img->user_data);
        img->image_layouts[sc->current_image] = MEL_GPU_IMAGE_LAYOUT_TRANSFER_SRC;
    }
    else
    {
        img->image_layouts[sc->current_image] = MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT;
    }

    img->current_frame = (img->current_frame + 1) % img->frame_count;
}

static void image_resize(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height)
{
    Mel_Image_Swapchain* img = sc->data;

    vkDeviceWaitIdle(mel__gpu_device_vk(dev)->device);

    destroy_images(sc, dev);

    if (img->has_staging)
        mel_gpu_buffer_shutdown(&img->staging, dev);

    sc->extent_width = width;
    sc->extent_height = height;
    sc->image_count = img->frame_count;

    create_images(sc, dev);

    if (img->on_present)
    {
        u32 pixel_size = mel_gpu_format_size(sc->format);
        mel_gpu_buffer_init(&img->staging, dev,
            .size = (u64)width * height * pixel_size,
            .usage = MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = MEL_GPU_MEMORY_USAGE_GPU_TO_CPU,
            .map_on_create = true);
        img->has_staging = true;
    }
}

static void image_shutdown(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Image_Swapchain* img = sc->data;
    if (!img) return;

    vkDeviceWaitIdle(mel__gpu_device_vk(dev)->device);

    if (img->has_staging)
        mel_gpu_buffer_shutdown(&img->staging, dev);

    destroy_images(sc, dev);

    const Mel_Alloc* alloc = img->alloc;
    mel_dealloc(alloc, img);
    sc->data = nullptr;
}

static Mel_Gpu_Image_Layout image_current_image_layout(Mel_Swapchain* sc)
{
    Mel_Image_Swapchain* img = sc->data;
    if (!img->image_layouts)
        return MEL_GPU_IMAGE_LAYOUT_UNDEFINED;
    return img->image_layouts[sc->current_image];
}

static const Mel_Swapchain_Vtable image_vtable = {
    .acquire             = image_acquire,
    .prepare_present     = image_prepare_present,
    .present             = image_present,
    .resize              = image_resize,
    .shutdown            = image_shutdown,
    .current_image_layout = image_current_image_layout,
};

bool mel_swapchain_image_init_opt(Mel_Swapchain* sc, Mel_Gpu_Device* dev, Mel_Swapchain_Image_Opt opt)
{
    assert(sc != nullptr);
    assert(dev != nullptr);
    assert(opt.width > 0 && opt.height > 0);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Gpu_Format format = opt.format ? opt.format : MEL_GPU_FORMAT_B8G8R8A8_SRGB;
    u32 frame_count = opt.frame_count > 0 ? opt.frame_count : 2;

    Mel_Image_Swapchain* img = mel_alloc_type(alloc, Mel_Image_Swapchain);
    *img = (Mel_Image_Swapchain){
        .alloc = alloc,
        .on_present = opt.on_present,
        .user_data = opt.user_data,
        .frame_count = frame_count,
    };

    *sc = (Mel_Swapchain){
        .vtable = &image_vtable,
        .data = img,
        .format = format,
        .extent_width = opt.width,
        .extent_height = opt.height,
        .image_count = frame_count,
    };

    if (!create_images(sc, dev))
    {
        destroy_images(sc, dev);
        mel_dealloc(alloc, img);
        sc->data = nullptr;
        return false;
    }

    if (opt.on_present)
    {
        u32 pixel_size = mel_gpu_format_size(format);
        mel_gpu_buffer_init(&img->staging, dev,
            .size = (u64)opt.width * opt.height * pixel_size,
            .usage = MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = MEL_GPU_MEMORY_USAGE_GPU_TO_CPU,
            .map_on_create = true);
        img->has_staging = true;
    }

    return true;
}
