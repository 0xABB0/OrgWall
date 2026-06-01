#include "vk_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

VkRenderPass mel_gpu__make_render_pass(Mel_Gpu_Device* dev, VkFormat color)
{
    VkAttachmentDescription att = {
        .format = color,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference ref = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription  sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &ref };
    VkSubpassDependency   dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &att,
        .subpassCount = 1,
        .pSubpasses = &sub,
        .dependencyCount = 1,
        .pDependencies = &dep,
    };
    VkRenderPass rp = VK_NULL_HANDLE;
    vkCreateRenderPass(dev->vk, &ci, NULL, &rp);
    return rp;
}

static void mel_gpu__choose_format(Mel_Gpu_Swapchain* sc, Mel_Gpu_Format requested)
{
    u32 count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(sc->dev->phys, sc->surface->vk, &count, NULL);
    VkSurfaceFormatKHR* formats = mel_alloc_array(mel_alloc_heap(), VkSurfaceFormatKHR, count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(sc->dev->phys, sc->surface->vk, &count, formats);

    VkFormat want = mel_gpu__vk_format(requested);
    sc->format = formats[0].format;
    sc->color_space = formats[0].colorSpace;

    bool found = false;
    for (u32 i = 0; i < count && !found; i++)
    {
        if (formats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            continue;
        if (want != VK_FORMAT_UNDEFINED && formats[i].format == want)
        {
            sc->format = formats[i].format;
            sc->color_space = formats[i].colorSpace;
            found = true;
        }
        else if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
        {
            sc->format = formats[i].format;
            sc->color_space = formats[i].colorSpace;
            found = true;
        }
    }
    if (!found)
    {
        for (u32 i = 0; i < count; i++)
            if (formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                sc->format = formats[i].format;
                sc->color_space = formats[i].colorSpace;
                break;
            }
    }
    mel_dealloc(mel_alloc_heap(), formats);
}

static VkPresentModeKHR mel_gpu__choose_present_mode(Mel_Gpu_Swapchain* sc)
{
    if (sc->vsync)
        return VK_PRESENT_MODE_FIFO_KHR;
    u32 count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(sc->dev->phys, sc->surface->vk, &count, NULL);
    VkPresentModeKHR* modes = mel_alloc_array(mel_alloc_heap(), VkPresentModeKHR, count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(sc->dev->phys, sc->surface->vk, &count, modes);
    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for (u32 i = 0; i < count; i++)
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            chosen = VK_PRESENT_MODE_MAILBOX_KHR;
    mel_dealloc(mel_alloc_heap(), modes);
    return chosen;
}

static void mel_gpu__images_teardown(Mel_Gpu_Swapchain* sc)
{
    for (u32 i = 0; i < sc->image_count; i++)
    {
        if (sc->framebuffers && sc->framebuffers[i])
            vkDestroyFramebuffer(sc->dev->vk, sc->framebuffers[i], NULL);
        if (sc->views && sc->views[i])
            vkDestroyImageView(sc->dev->vk, sc->views[i], NULL);
        if (sc->render_finished && sc->render_finished[i])
            vkDestroySemaphore(sc->dev->vk, sc->render_finished[i], NULL);
    }
    const Mel_Alloc* a = mel_alloc_heap();
    if (sc->images)
        mel_dealloc(a, sc->images);
    if (sc->views)
        mel_dealloc(a, sc->views);
    if (sc->framebuffers)
        mel_dealloc(a, sc->framebuffers);
    if (sc->render_finished)
        mel_dealloc(a, sc->render_finished);
    sc->images = NULL;
    sc->views = NULL;
    sc->framebuffers = NULL;
    sc->render_finished = NULL;
    sc->image_count = 0;
}

static bool mel_gpu__images_build(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    Mel_Gpu_Device*          dev = sc->dev;
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->phys, sc->surface->vk, &caps);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        extent.width = (u32)width;
        extent.height = (u32)height;
    }
    if (extent.width < caps.minImageExtent.width)
        extent.width = caps.minImageExtent.width;
    if (extent.height < caps.minImageExtent.height)
        extent.height = caps.minImageExtent.height;
    if (extent.width > caps.maxImageExtent.width)
        extent.width = caps.maxImageExtent.width;
    if (extent.height > caps.maxImageExtent.height)
        extent.height = caps.maxImageExtent.height;
    sc->extent = extent;

    u32 want = caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount)
        want = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = sc->surface->vk,
        .minImageCount = want,
        .imageFormat = sc->format,
        .imageColorSpace = sc->color_space,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = mel_gpu__choose_present_mode(sc),
        .clipped = VK_TRUE,
    };
    VkResult r = vkCreateSwapchainKHR(dev->vk, &ci, NULL, &sc->vk);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkCreateSwapchainKHR failed: %s", mel_gpu__vk_result_str(r));
        return false;
    }

    u32 count = 0;
    vkGetSwapchainImagesKHR(dev->vk, sc->vk, &count, NULL);
    const Mel_Alloc* a = mel_alloc_heap();
    sc->image_count = count;
    sc->images = mel_alloc_array(a, VkImage, count);
    sc->views = mel_alloc_array(a, VkImageView, count);
    sc->framebuffers = sc->dev->dynamic_rendering ? NULL : mel_alloc_array(a, VkFramebuffer, count);
    sc->render_finished = mel_alloc_array(a, VkSemaphore, count);
    vkGetSwapchainImagesKHR(dev->vk, sc->vk, &count, sc->images);

    for (u32 i = 0; i < count; i++)
    {
        VkImageViewCreateInfo vci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = sc->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = sc->format,
            .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
        };
        vkCreateImageView(dev->vk, &vci, NULL, &sc->views[i]);

        if (!dev->dynamic_rendering)
        {
            VkFramebufferCreateInfo fci = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = sc->render_pass,
                .attachmentCount = 1,
                .pAttachments = &sc->views[i],
                .width = extent.width,
                .height = extent.height,
                .layers = 1,
            };
            vkCreateFramebuffer(dev->vk, &fci, NULL, &sc->framebuffers[i]);
        }

        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(dev->vk, &sci, NULL, &sc->render_finished[i]);
    }
    return true;
}

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.surface)
        return NULL;

    Mel_Gpu_Swapchain* sc = mel_alloc_type(mel_alloc_heap(), Mel_Gpu_Swapchain);
    *sc = (Mel_Gpu_Swapchain){ 0 };
    sc->dev = dev;
    sc->surface = opt.surface;
    sc->vsync = opt.vsync;
    sc->frames_in_flight = 2;
    sc->recorder.dev = dev;
    sc->recorder.sc = sc;

    mel_gpu__choose_format(sc, opt.format);
    if (!dev->dynamic_rendering)
        sc->render_pass = mel_gpu__make_render_pass(dev, sc->format);

    if (!mel_gpu__images_build(sc, opt.width, opt.height))
    {
        if (sc->render_pass)
            vkDestroyRenderPass(dev->vk, sc->render_pass, NULL);
        mel_dealloc(mel_alloc_heap(), sc);
        return NULL;
    }

    const Mel_Alloc*        a = mel_alloc_heap();
    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = dev->graphics_family };
    vkCreateCommandPool(dev->vk, &pci, NULL, &sc->cmd_pool);

    sc->cmd_buffers = mel_alloc_array(a, VkCommandBuffer, sc->frames_in_flight);
    sc->image_available = mel_alloc_array(a, VkSemaphore, sc->frames_in_flight);
    sc->in_flight = mel_alloc_array(a, VkFence, sc->frames_in_flight);
    sc->frame_serial = mel_alloc_array(a, u64, sc->frames_in_flight);

    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = sc->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = sc->frames_in_flight };
    vkAllocateCommandBuffers(dev->vk, &cai, sc->cmd_buffers);

    for (u32 i = 0; i < sc->frames_in_flight; i++)
    {
        sc->frame_serial[i] = 0;
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(dev->vk, &sci, NULL, &sc->image_available[i]);
        VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        vkCreateFence(dev->vk, &fci, NULL, &sc->in_flight[i]);
    }

    return sc;
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc)
        return;
    vkDeviceWaitIdle(sc->dev->vk);
    mel_gpu__images_teardown(sc);
    if (sc->vk)
        vkDestroySwapchainKHR(sc->dev->vk, sc->vk, NULL);
    sc->vk = VK_NULL_HANDLE;
    mel_gpu__images_build(sc, width, height);
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc)
        return;
    Mel_Gpu_Device* dev = sc->dev;
    vkDeviceWaitIdle(dev->vk);

    for (u32 i = 0; i < sc->frames_in_flight; i++)
    {
        if (sc->image_available[i])
            vkDestroySemaphore(dev->vk, sc->image_available[i], NULL);
        if (sc->in_flight[i])
            vkDestroyFence(dev->vk, sc->in_flight[i], NULL);
    }
    const Mel_Alloc* a = mel_alloc_heap();
    mel_dealloc(a, sc->cmd_buffers);
    mel_dealloc(a, sc->image_available);
    mel_dealloc(a, sc->in_flight);
    mel_dealloc(a, sc->frame_serial);
    if (sc->cmd_pool)
        vkDestroyCommandPool(dev->vk, sc->cmd_pool, NULL);

    mel_gpu__images_teardown(sc);
    if (sc->vk)
        vkDestroySwapchainKHR(dev->vk, sc->vk, NULL);
    if (sc->render_pass)
        vkDestroyRenderPass(dev->vk, sc->render_pass, NULL);
    mel_dealloc(a, sc);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc) { return sc ? mel_gpu__vk_format_to_mel(sc->format) : MEL_GPU_FORMAT_UNDEFINED; }
