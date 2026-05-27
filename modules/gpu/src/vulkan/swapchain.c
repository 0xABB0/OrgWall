#include "vulkan_backend.h"

static void destroy_swapchain_objects(Mel_Gpu_Swapchain* sc)
{
    VkDevice d = sc->device->device;
    for (u32 i = 0; i < sc->image_count; i++) {
        if (sc->framebuffers[i]) vkDestroyFramebuffer(d, sc->framebuffers[i], NULL);
        if (sc->views[i])        vkDestroyImageView(d, sc->views[i], NULL);
    }
    sc->image_count = 0;
    if (sc->swapchain) { vkDestroySwapchainKHR(d, sc->swapchain, NULL); sc->swapchain = VK_NULL_HANDLE; }
}

static bool create_swapchain_objects(Mel_Gpu_Swapchain* sc)
{
    Mel_Gpu_Device* dev = sc->device;
    VkDevice        d   = dev->device;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->phys, sc->surface, &caps);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width  = (u32)sc->req_width;
        extent.height = (u32)sc->req_height;
    }
    if (extent.width == 0 || extent.height == 0) return false;
    sc->extent = extent;

    u32 image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    if (image_count > MEL_GPU_VK_MAX_IMAGES) image_count = MEL_GPU_VK_MAX_IMAGES;

    VkSwapchainCreateInfoKHR ci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = sc->surface,
        .minImageCount    = image_count,
        .imageFormat      = sc->format,
        .imageColorSpace  = sc->color_space,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = VK_TRUE,
    };
    if (vkCreateSwapchainKHR(d, &ci, NULL, &sc->swapchain) != VK_SUCCESS) return false;

    vkGetSwapchainImagesKHR(d, sc->swapchain, &sc->image_count, NULL);
    if (sc->image_count > MEL_GPU_VK_MAX_IMAGES) sc->image_count = MEL_GPU_VK_MAX_IMAGES;
    vkGetSwapchainImagesKHR(d, sc->swapchain, &sc->image_count, sc->images);

    for (u32 i = 0; i < sc->image_count; i++) {
        VkImageViewCreateInfo vci = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = sc->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = sc->format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        if (vkCreateImageView(d, &vci, NULL, &sc->views[i]) != VK_SUCCESS) return false;

        VkFramebufferCreateInfo fci = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = sc->render_pass,
            .attachmentCount = 1,
            .pAttachments    = &sc->views[i],
            .width           = extent.width,
            .height          = extent.height,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(d, &fci, NULL, &sc->framebuffers[i]) != VK_SUCCESS) return false;
    }
    return true;
}

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.native_window) return NULL;

    Mel_Gpu_Swapchain* sc = calloc(1, sizeof *sc);
    if (!sc) return NULL;
    sc->device     = dev;
    sc->mel_format = opt.format == MEL_GPU_FORMAT_UNDEFINED ? MEL_GPU_FORMAT_BGRA8_UNORM : opt.format;
    sc->format     = mel_gpu__vk_color_format(sc->mel_format);
    sc->req_width  = opt.width  > 0 ? opt.width  : 1;
    sc->req_height = opt.height > 0 ? opt.height : 1;
    sc->cmd.swapchain = sc;

#if defined(__APPLE__)
    sc->metal_layer = mel_gpu__vk_make_metal_layer(opt.native_window);
    if (!sc->metal_layer || mel_gpu__vk_create_metal_surface(dev->instance, sc->metal_layer, &sc->surface) != VK_SUCCESS) {
        free(sc);
        return NULL;
    }
#elif defined(__ANDROID__)
    if (mel_gpu__vk_create_android_surface(dev->instance, opt.native_window, &sc->surface) != VK_SUCCESS) {
        free(sc);
        return NULL;
    }
#else
    free(sc);
    return NULL; // no window-system surface implemented for this platform yet
#endif

    VkBool32 present_ok = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(dev->phys, dev->queue_family, sc->surface, &present_ok);

    // Pick a surface format the driver actually supports. The requested format
    // (BGRA8 default) is honored when offered, but Android's gralloc swapchain
    // only supports RGBA8, so fall back to it rather than failing.
    sc->color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    {
        u32 fc = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev->phys, sc->surface, &fc, NULL);
        VkSurfaceFormatKHR fmts[32];
        if (fc > 32) fc = 32;
        if (fc > 0) {
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev->phys, sc->surface, &fc, fmts);
            int chosen = -1;
            for (u32 i = 0; i < fc; i++) if (fmts[i].format == sc->format)                  { chosen = (int)i; break; }
            if (chosen < 0) for (u32 i = 0; i < fc; i++) if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) { chosen = (int)i; break; }
            if (chosen < 0) for (u32 i = 0; i < fc; i++) if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = (int)i; break; }
            if (chosen < 0) chosen = 0;
            sc->format      = fmts[chosen].format;
            sc->color_space = fmts[chosen].colorSpace;
            sc->mel_format  = mel_gpu__vk_mel_format(sc->format);
        }
    }

    sc->render_pass = mel_gpu__vk_make_render_pass(dev->device, sc->format);
    if (!sc->render_pass) { mel_gpu_swapchain_destroy(sc); return NULL; }

    VkCommandBufferAllocateInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = dev->cmd_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(dev->device, &ai, &sc->cmd_buffer);

    VkSemaphoreCreateInfo sem = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fen = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                  .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    vkCreateSemaphore(dev->device, &sem, NULL, &sc->image_available);
    vkCreateSemaphore(dev->device, &sem, NULL, &sc->render_finished);
    vkCreateFence(dev->device, &fen, NULL, &sc->in_flight);

    if (!create_swapchain_objects(sc)) { mel_gpu_swapchain_destroy(sc); return NULL; }
    return sc;
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc) return;
    VkDevice d = sc->device->device;
    vkDeviceWaitIdle(d);

    destroy_swapchain_objects(sc);
    if (sc->in_flight)       vkDestroyFence(d, sc->in_flight, NULL);
    if (sc->image_available) vkDestroySemaphore(d, sc->image_available, NULL);
    if (sc->render_finished) vkDestroySemaphore(d, sc->render_finished, NULL);
    if (sc->cmd_buffer)      vkFreeCommandBuffers(d, sc->device->cmd_pool, 1, &sc->cmd_buffer);
    if (sc->render_pass)     vkDestroyRenderPass(d, sc->render_pass, NULL);
    if (sc->surface)         vkDestroySurfaceKHR(sc->device->instance, sc->surface, NULL);
#ifdef __APPLE__
    if (sc->metal_layer)     mel_gpu__vk_release_metal_layer(sc->metal_layer);
#endif
    free(sc);
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc || width <= 0 || height <= 0) return;
    sc->req_width  = width;
    sc->req_height = height;
#ifdef __APPLE__
    mel_gpu__vk_layer_set_size(sc->metal_layer, width, height);
#endif
    vkDeviceWaitIdle(sc->device->device);
    destroy_swapchain_objects(sc);
    create_swapchain_objects(sc);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc)
{
    return sc ? sc->mel_format : MEL_GPU_FORMAT_UNDEFINED;
}
