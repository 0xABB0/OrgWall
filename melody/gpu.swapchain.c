#define VK_NO_PROTOTYPES
#include "gpu.swapchain.h"
#include "swapchain.h"
#include "window.h"
#include "gpu.device.h"
#include "allocator.h"
#include "allocator.heap.h"
#include <tracy/TracyC.h>

typedef struct {
    VkSwapchainKHR handle;
    VkSurfaceKHR surface;
    VkColorSpaceKHR color_space;
    VkPresentModeKHR present_mode;
    const Mel_Alloc* alloc;

    VkSemaphore* image_available;
    VkSemaphore* render_finished;
    VkImageLayout* image_layouts;
    u32 frame_count;
    u32 current_frame;
} Mel_Gpu_Swapchain;

static const char* present_mode_name(VkPresentModeKHR mode);

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

static VkPresentModeKHR choose_present_mode(Mel_Gpu_Device* dev, VkSurfaceKHR surface,
                                            VkPresentModeKHR preferred)
{
    u32 count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev->physical_device, surface, &count, nullptr);
    if (count == 0)
        return VK_PRESENT_MODE_FIFO_KHR;

    VkPresentModeKHR* modes = mel_alloc(mel_alloc_heap(), sizeof(VkPresentModeKHR) * count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev->physical_device, surface, &count, modes);

    char mode_buf[256] = {0};
    size off = 0;
    for (u32 i = 0; i < count; i++)
    {
        int wrote = SDL_snprintf(mode_buf + off, sizeof(mode_buf) - off,
            "%s%s", i > 0 ? ", " : "", present_mode_name(modes[i]));
        if (wrote <= 0 || (size)wrote >= (size)(sizeof(mode_buf) - off))
            break;
        off += (size)wrote;
    }
    SDL_Log("Swapchain present modes: %s", mode_buf);

    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for (u32 i = 0; i < count; i++)
    {
        if (modes[i] == preferred)
        {
            chosen = preferred;
            goto done;
        }
    }

    for (u32 i = 0; i < count; i++)
    {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            chosen = VK_PRESENT_MODE_MAILBOX_KHR;
            goto done;
        }
    }

    for (u32 i = 0; i < count; i++)
    {
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            chosen = VK_PRESENT_MODE_IMMEDIATE_KHR;
            goto done;
        }
    }

done:
    mel_dealloc(mel_alloc_heap(), modes);
    return chosen;
}

static const char* present_mode_name(VkPresentModeKHR mode)
{
    switch (mode)
    {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
    case VK_PRESENT_MODE_FIFO_KHR: return "fifo";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "fifo_relaxed";
    default: return "unknown";
    }
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

static bool create_swapchain(Mel_Swapchain* sc, Mel_Gpu_Device* dev,
                             u32 width, u32 height, VkSwapchainKHR old)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    const Mel_Alloc* alloc = khr->alloc;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->physical_device, khr->surface, &caps);

    u32 format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physical_device, khr->surface, &format_count, nullptr);
    VkSurfaceFormatKHR* formats = mel_alloc(alloc, sizeof(VkSurfaceFormatKHR) * format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physical_device, khr->surface, &format_count, formats);

    VkSurfaceFormatKHR format = choose_format(formats, format_count);
    SDL_Log("Swapchain surface formats: %u, chosen format=%u colorSpace=%u",
        format_count, format.format, format.colorSpace);
    mel_dealloc(alloc, formats);

    VkExtent2D extent = choose_extent(&caps, width, height);
    SDL_Log("Swapchain surface caps: min=%ux%u max=%ux%u current=%ux%u minImages=%u maxImages=%u",
        caps.minImageExtent.width, caps.minImageExtent.height,
        caps.maxImageExtent.width, caps.maxImageExtent.height,
        caps.currentExtent.width, caps.currentExtent.height,
        caps.minImageCount, caps.maxImageCount);

    u32 image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = khr->surface,
        .minImageCount = image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = khr->present_mode,
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

    VkResult r = vkCreateSwapchainKHR(dev->device, &create_info, nullptr, &khr->handle);
    if (r != VK_SUCCESS)
    {
        SDL_Log("Failed to create swapchain: %d", r);
        return false;
    }

    sc->format = format.format;
    khr->color_space = format.colorSpace;
    sc->extent = extent;

    vkGetSwapchainImagesKHR(dev->device, khr->handle, &sc->image_count, nullptr);
    sc->images = mel_alloc(alloc, sizeof(VkImage) * sc->image_count);
    vkGetSwapchainImagesKHR(dev->device, khr->handle, &sc->image_count, sc->images);

    sc->image_views = mel_alloc(alloc, sizeof(VkImageView) * sc->image_count);
    khr->image_layouts = mel_alloc(alloc, sizeof(VkImageLayout) * sc->image_count);
    for (u32 i = 0; i < sc->image_count; i++)
    {
        khr->image_layouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
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
        if (rv != VK_SUCCESS)
        {
            SDL_Log("Failed to create swapchain image view %u: %d", i, rv);
            sc->image_count = i;
            return false;
        }
    }
    return true;
}

static bool create_sync(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    const Mel_Alloc* alloc = khr->alloc;

    khr->image_available = mel_alloc(alloc, sizeof(VkSemaphore) * khr->frame_count);
    khr->render_finished = mel_alloc(alloc, sizeof(VkSemaphore) * sc->image_count);

    VkSemaphoreCreateInfo sem_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    for (u32 i = 0; i < khr->frame_count; i++)
    {
        VkResult r = vkCreateSemaphore(dev->device, &sem_info, nullptr, &khr->image_available[i]);
        if (r != VK_SUCCESS)
        {
            SDL_Log("Failed to create image_available semaphore %u: %d", i, r);
            return false;
        }
    }

    for (u32 i = 0; i < sc->image_count; i++)
    {
        VkResult r = vkCreateSemaphore(dev->device, &sem_info, nullptr, &khr->render_finished[i]);
        if (r != VK_SUCCESS)
        {
            SDL_Log("Failed to create render_finished semaphore %u: %d", i, r);
            return false;
        }
    }

    return true;
}

static void destroy_sync(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    const Mel_Alloc* alloc = khr->alloc;

    if (khr->render_finished)
    {
        for (u32 i = 0; i < sc->image_count; i++)
            vkDestroySemaphore(dev->device, khr->render_finished[i], nullptr);
        mel_dealloc(alloc, khr->render_finished);
        khr->render_finished = nullptr;
    }

    if (khr->image_available)
    {
        for (u32 i = 0; i < khr->frame_count; i++)
            vkDestroySemaphore(dev->device, khr->image_available[i], nullptr);
        mel_dealloc(alloc, khr->image_available);
        khr->image_available = nullptr;
    }
}

static void destroy_resources(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    const Mel_Alloc* alloc = khr->alloc;

    if (sc->image_views)
    {
        for (u32 i = 0; i < sc->image_count; i++)
            vkDestroyImageView(dev->device, sc->image_views[i], nullptr);
        mel_dealloc(alloc, sc->image_views);
        sc->image_views = nullptr;
    }

    if (khr->image_layouts)
    {
        mel_dealloc(alloc, khr->image_layouts);
        khr->image_layouts = nullptr;
    }

    if (sc->images)
    {
        mel_dealloc(alloc, sc->images);
        sc->images = nullptr;
    }

    if (khr->handle)
    {
        vkDestroySwapchainKHR(dev->device, khr->handle, nullptr);
        khr->handle = VK_NULL_HANDLE;
    }
}

static bool khr_acquire(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    TracyCZoneN(ctx, "swapchain_acquire", true);

    VkResult result = vkAcquireNextImageKHR(
        dev->device, khr->handle, UINT64_MAX,
        khr->image_available[khr->current_frame], VK_NULL_HANDLE,
        &sc->current_image);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        TracyCZoneEnd(ctx);
        return false;
    }

    assert(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
    TracyCZoneEnd(ctx);
    return true;
}

static void khr_prepare_present(Mel_Swapchain* sc, VkCommandBuffer cmd)
{
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = sc->images[sc->current_image],
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
    vkCmdPipelineBarrier2(cmd, &dep);
}

static void khr_collect_sync(Mel_Swapchain* sc, Mel_Gpu_Submit_Gather* gather)
{
    Mel_Gpu_Swapchain* khr = sc->data;

    assert(gather->wait_count < MEL_MAX_SWAPCHAINS);
    gather->wait_semaphores[gather->wait_count] = khr->image_available[khr->current_frame];
    gather->wait_stages[gather->wait_count] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    gather->wait_count++;

    assert(gather->signal_count < MEL_MAX_SWAPCHAINS);
    gather->signal_semaphores[gather->signal_count] = khr->render_finished[sc->current_image];
    gather->signal_count++;
}

static void khr_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    TracyCZoneN(ctx, "swapchain_present", true);

    VkSemaphore signal = khr->render_finished[sc->current_image];

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &signal,
        .swapchainCount = 1,
        .pSwapchains = &khr->handle,
        .pImageIndices = &sc->current_image,
    };

    VkResult pr = vkQueuePresentKHR(dev->present_queue, &present_info);

    if (khr->image_layouts)
        khr->image_layouts[sc->current_image] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    khr->current_frame = (khr->current_frame + 1) % khr->frame_count;

    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
    {
        TracyCZoneEnd(ctx);
        return;
    }

    assert(pr == VK_SUCCESS);
    TracyCZoneEnd(ctx);
}

static void khr_resize(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height)
{
    Mel_Gpu_Swapchain* khr = sc->data;

    assert(khr->handle != VK_NULL_HANDLE);

    vkDeviceWaitIdle(dev->device);

    destroy_sync(sc, dev);

    VkSwapchainKHR old = khr->handle;
    const Mel_Alloc* alloc = khr->alloc;

    for (u32 i = 0; i < sc->image_count; i++)
        vkDestroyImageView(dev->device, sc->image_views[i], nullptr);
    mel_dealloc(alloc, sc->image_views);
    mel_dealloc(alloc, sc->images);

    khr->handle = VK_NULL_HANDLE;
    sc->image_views = nullptr;
    sc->images = nullptr;

    create_swapchain(sc, dev, width, height, old);
    vkDestroySwapchainKHR(dev->device, old, nullptr);

    create_sync(sc, dev);
    khr->current_frame = 0;
}

static VkImageLayout khr_current_image_layout(Mel_Swapchain* sc)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    if (!khr->image_layouts)
        return VK_IMAGE_LAYOUT_UNDEFINED;
    return khr->image_layouts[sc->current_image];
}

static VkPresentModeKHR khr_present_mode(Mel_Swapchain* sc)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    return khr ? khr->present_mode : VK_PRESENT_MODE_FIFO_KHR;
}

static void khr_shutdown(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    Mel_Gpu_Swapchain* khr = sc->data;
    if (!khr) return;

    vkDeviceWaitIdle(dev->device);
    destroy_sync(sc, dev);
    destroy_resources(sc, dev);

    const Mel_Alloc* alloc = khr->alloc;
    mel_dealloc(alloc, khr);
    sc->data = nullptr;
}

static const Mel_Swapchain_Vtable khr_vtable = {
    .acquire             = khr_acquire,
    .prepare_present     = khr_prepare_present,
    .collect_sync        = khr_collect_sync,
    .present             = khr_present,
    .resize              = khr_resize,
    .shutdown            = khr_shutdown,
    .current_image_layout = khr_current_image_layout,
    .present_mode        = khr_present_mode,
};

bool mel_gpu_swapchain_init_opt(Mel_Swapchain* sc, Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    assert(sc != nullptr);
    assert(dev != nullptr);
    assert(opt.surface != VK_NULL_HANDLE);
    assert(opt.width > 0 && opt.height > 0);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Gpu_Swapchain* khr = mel_alloc_type(alloc, Mel_Gpu_Swapchain);
    *khr = (Mel_Gpu_Swapchain){
        .surface = opt.surface,
        .present_mode = choose_present_mode(dev, opt.surface,
            opt.preferred_present_mode ? opt.preferred_present_mode : VK_PRESENT_MODE_FIFO_KHR),
        .alloc = alloc,
        .frame_count = opt.frame_count > 0 ? opt.frame_count : 2,
    };

    SDL_Log("Swapchain present mode: requested=%s chosen=%s",
        present_mode_name(opt.preferred_present_mode ? opt.preferred_present_mode : VK_PRESENT_MODE_FIFO_KHR),
        present_mode_name(khr->present_mode));

    *sc = (Mel_Swapchain){
        .vtable = &khr_vtable,
        .data = khr,
    };

    if (!create_swapchain(sc, dev, opt.width, opt.height, VK_NULL_HANDLE))
    {
        destroy_resources(sc, dev);
        mel_dealloc(alloc, khr);
        sc->data = nullptr;
        return false;
    }

    if (!create_sync(sc, dev))
    {
        destroy_sync(sc, dev);
        destroy_resources(sc, dev);
        mel_dealloc(alloc, khr);
        sc->data = nullptr;
        return false;
    }

    return true;
}

Mel_Swapchain_Handle mel_gpu_swapchain_create_for_window(Mel_Gpu_Device* dev, Mel_Window_Handle window)
{
    return mel_gpu_swapchain_create_for_window_opt(dev, window, (Mel_Gpu_Window_Swapchain_Opt){0});
}

Mel_Swapchain_Handle mel_gpu_swapchain_create_for_window_opt(Mel_Gpu_Device* dev, Mel_Window_Handle window, Mel_Gpu_Window_Swapchain_Opt opt)
{
    assert(dev != nullptr);
    assert(mel_window_handle_valid(window));

    SDL_Window* sdl = mel__window_sdl(window);
    VkSurfaceKHR surface = mel_gpu_surface_create(dev, sdl);
    if (surface == VK_NULL_HANDLE)
        return MEL_SWAPCHAIN_HANDLE_NULL;

    i32 w, h;
    SDL_GetWindowSizeInPixels(sdl, &w, &h);

    Mel_Swapchain_Entry entry = {
        .surface = surface,
        .window = window,
    };

    if (!mel_gpu_swapchain_init(&entry.swapchain, dev,
        .surface = surface,
        .width = (u32)w,
        .height = (u32)h,
        .frame_count = opt.frame_count,
        .preferred_present_mode = opt.preferred_present_mode,
        .alloc = opt.alloc))
    {
        mel_gpu_surface_destroy(dev, surface);
        return MEL_SWAPCHAIN_HANDLE_NULL;
    }

    return mel_swapchain_registry_insert(&entry);
}
