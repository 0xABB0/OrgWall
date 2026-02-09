#define VK_NO_PROTOTYPES
#include "gpu.swapchain.h"
#include "allocator.heap.h"

static VkSurfaceFormatKHR choose_format(VkSurfaceFormatKHR* formats, u32 count)
{
    for (u32 i = 0; i < count; i++)
    {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return formats[i];
    }
    return formats[0];
}

static VkExtent2D choose_extent(VkSurfaceCapabilitiesKHR* caps, u32 width, u32 height)
{
    if (caps->currentExtent.width != UINT32_MAX)
        return caps->currentExtent;

    VkExtent2D extent = { width, height };
    if (extent.width < caps->minImageExtent.width) extent.width = caps->minImageExtent.width;
    if (extent.width > caps->maxImageExtent.width) extent.width = caps->maxImageExtent.width;
    if (extent.height < caps->minImageExtent.height) extent.height = caps->minImageExtent.height;
    if (extent.height > caps->maxImageExtent.height) extent.height = caps->maxImageExtent.height;
    return extent;
}

static void create_swapchain(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev,
                             u32 width, u32 height, VkSwapchainKHR old)
{
    const Mel_Alloc* alloc = sc->alloc ? sc->alloc : mel_alloc_heap();

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->physical_device, dev->surface, &caps);

    u32 format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physical_device, dev->surface, &format_count, nullptr);
    VkSurfaceFormatKHR* formats = mel_alloc(alloc, sizeof(VkSurfaceFormatKHR) * format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physical_device, dev->surface, &format_count, formats);

    VkSurfaceFormatKHR format = choose_format(formats, format_count);
    mel_dealloc(alloc, formats);

    VkExtent2D extent = choose_extent(&caps, width, height);

    u32 image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = dev->surface,
        .minImageCount = image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = sc->present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };

    u32 families[] = { dev->graphics_family, dev->present_family };
    if (dev->graphics_family != dev->present_family)
    {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = families;
    }
    else
    {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult r = vkCreateSwapchainKHR(dev->device, &create_info, nullptr, &sc->swapchain);
    assert(r == VK_SUCCESS);

    sc->format = format.format;
    sc->color_space = format.colorSpace;
    sc->extent = extent;

    vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &sc->image_count, nullptr);
    sc->images = mel_alloc(alloc, sizeof(VkImage) * sc->image_count);
    vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &sc->image_count, sc->images);

    sc->image_views = mel_alloc(alloc, sizeof(VkImageView) * sc->image_count);
    for (u32 i = 0; i < sc->image_count; i++)
    {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = sc->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = sc->format,
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

        VkResult rv = vkCreateImageView(dev->device, &view_info, nullptr, &sc->image_views[i]);
        assert(rv == VK_SUCCESS);
    }
}

static void destroy_swapchain_resources(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev)
{
    const Mel_Alloc* alloc = sc->alloc ? sc->alloc : mel_alloc_heap();

    if (sc->image_views)
    {
        for (u32 i = 0; i < sc->image_count; i++)
            vkDestroyImageView(dev->device, sc->image_views[i], nullptr);
        mel_dealloc(alloc, sc->image_views);
        sc->image_views = nullptr;
    }

    if (sc->images)
    {
        mel_dealloc(alloc, sc->images);
        sc->images = nullptr;
    }

    if (sc->swapchain)
    {
        vkDestroySwapchainKHR(dev->device, sc->swapchain, nullptr);
        sc->swapchain = VK_NULL_HANDLE;
    }
}

void mel_gpu_swapchain_init_opt(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    assert(sc != nullptr);
    assert(dev != nullptr);
    assert(opt.width > 0 && opt.height > 0);

    *sc = (Mel_Gpu_Swapchain){0};
    sc->alloc = opt.alloc;
    sc->present_mode = opt.preferred_present_mode ? opt.preferred_present_mode : VK_PRESENT_MODE_FIFO_KHR;

    create_swapchain(sc, dev, opt.width, opt.height, VK_NULL_HANDLE);
}

void mel_gpu_swapchain_shutdown(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev)
{
    assert(sc != nullptr);
    assert(dev != nullptr);

    vkDeviceWaitIdle(dev->device);
    destroy_swapchain_resources(sc, dev);
}

void mel_gpu_swapchain_recreate(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height)
{
    assert(sc != nullptr);
    assert(sc->swapchain != VK_NULL_HANDLE);

    vkDeviceWaitIdle(dev->device);

    VkSwapchainKHR old = sc->swapchain;
    const Mel_Alloc* alloc = sc->alloc ? sc->alloc : mel_alloc_heap();

    for (u32 i = 0; i < sc->image_count; i++)
        vkDestroyImageView(dev->device, sc->image_views[i], nullptr);
    mel_dealloc(alloc, sc->image_views);
    mel_dealloc(alloc, sc->images);

    sc->swapchain = VK_NULL_HANDLE;
    sc->image_views = nullptr;
    sc->images = nullptr;

    create_swapchain(sc, dev, width, height, old);
    vkDestroySwapchainKHR(dev->device, old, nullptr);
}

bool mel_gpu_swapchain_acquire(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, VkSemaphore signal_semaphore)
{
    assert(sc != nullptr);
    assert(dev != nullptr);
    assert(signal_semaphore != VK_NULL_HANDLE);

    VkResult result = vkAcquireNextImageKHR(
        dev->device, sc->swapchain, UINT64_MAX,
        signal_semaphore, VK_NULL_HANDLE, &sc->current_image);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
        return false;

    assert(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
    return true;
}

bool mel_gpu_swapchain_present(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, VkSemaphore wait_semaphore)
{
    assert(sc != nullptr);
    assert(dev != nullptr);
    assert(wait_semaphore != VK_NULL_HANDLE);

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &wait_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &sc->swapchain,
        .pImageIndices = &sc->current_image,
    };

    VkResult result = vkQueuePresentKHR(dev->present_queue, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        return false;

    assert(result == VK_SUCCESS);
    return true;
}
