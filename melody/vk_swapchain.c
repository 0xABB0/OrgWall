#define VK_NO_PROTOTYPES
#include "vk_swapchain.h"
#include <string.h>
#include <tracy/TracyC.h>
#include "allocator.h"
#include "allocator.heap.h"

#define VK_CHECK(expr) do { \
    VkResult _res = (expr); \
    assert(_res == VK_SUCCESS && #expr); \
} while (0)

static VkSurfaceFormatKHR choose_format(VkSurfaceFormatKHR* formats, u32 count)
{
    for (u32 i = 0; i < count; i++)
    {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return formats[i];
        }
    }
    return formats[0];
}

static VkPresentModeKHR choose_present_mode(VkPresentModeKHR* modes, u32 count)
{
    // for (u32 i = 0; i < count; i++)
    // {
    //     if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
    //     {
    //         return modes[i];
    //     }
    // }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_extent(VkSurfaceCapabilitiesKHR* caps, u32 width, u32 height)
{
    if (caps->currentExtent.width != UINT32_MAX)
    {
        return caps->currentExtent;
    }

    VkExtent2D extent = { width, height };

    if (extent.width < caps->minImageExtent.width)
        extent.width = caps->minImageExtent.width;
    if (extent.width > caps->maxImageExtent.width)
        extent.width = caps->maxImageExtent.width;
    if (extent.height < caps->minImageExtent.height)
        extent.height = caps->minImageExtent.height;
    if (extent.height > caps->maxImageExtent.height)
        extent.height = caps->maxImageExtent.height;

    return extent;
}

static bool create_swapchain(Mel_VkSwapchain* sc, Mel_VkContext* ctx, u32 width, u32 height, VkSwapchainKHR old)    
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device, ctx->surface, &caps);

    u32 format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &format_count, nullptr);
    VkSurfaceFormatKHR* formats = mel_alloc(mel_alloc_heap(), sizeof(VkSurfaceFormatKHR) * format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &format_count, formats);

    u32 mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &mode_count, nullptr);
    VkPresentModeKHR* modes = mel_alloc(mel_alloc_heap(), sizeof(VkPresentModeKHR) * mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &mode_count, modes);

    VkSurfaceFormatKHR format = choose_format(formats, format_count);
    VkPresentModeKHR mode = choose_present_mode(modes, mode_count);
    VkExtent2D extent = choose_extent(&caps, width, height);

    mel_dealloc(mel_alloc_heap(), formats);
    mel_dealloc(mel_alloc_heap(), modes);

    u32 image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
    {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx->surface,
        .minImageCount = image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };

    u32 families[] = { ctx->graphics_family, ctx->present_family };
    if (ctx->graphics_family != ctx->present_family)
    {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = families;
    }
    else
    {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(ctx->device, &create_info, nullptr, &sc->swapchain));

    sc->format = format.format;
    sc->extent = extent;

    vkGetSwapchainImagesKHR(ctx->device, sc->swapchain, &sc->image_count, nullptr);
    sc->images = mel_alloc(mel_alloc_heap(), sizeof(VkImage) * sc->image_count);
    vkGetSwapchainImagesKHR(ctx->device, sc->swapchain, &sc->image_count, sc->images);

    return true;
}

static bool create_image_views(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    sc->image_views = mel_alloc(mel_alloc_heap(), sizeof(VkImageView) * sc->image_count);

    for (u32 i = 0; i < sc->image_count; i++)
    {
        VkImageViewCreateInfo create_info = {
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

        VK_CHECK(vkCreateImageView(ctx->device, &create_info, nullptr, &sc->image_views[i]));
    }

    return true;
}

static bool create_command_pools(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_family,
    };

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateCommandPool(ctx->device, &pool_info, nullptr, &sc->command_pools[i]));

        alloc_info.commandPool = sc->command_pools[i];
        VK_CHECK(vkAllocateCommandBuffers(ctx->device, &alloc_info, &sc->command_buffers[i]));
    }

    return true;
}

static bool create_sync_objects(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateSemaphore(ctx->device, &sem_info, nullptr, &sc->image_available[i]));
        VK_CHECK(vkCreateSemaphore(ctx->device, &sem_info, nullptr, &sc->render_finished[i]));
        VK_CHECK(vkCreateFence(ctx->device, &fence_info, nullptr, &sc->in_flight[i]));
    }

    return true;
}

static void destroy_swapchain_resources(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    for (u32 i = 0; i < sc->image_count; i++)
    {
        if (sc->image_views && sc->image_views[i])
        {
            vkDestroyImageView(ctx->device, sc->image_views[i], nullptr);
        }
    }

    mel_dealloc(mel_alloc_heap(), sc->image_views);
    mel_dealloc(mel_alloc_heap(), sc->images);
    sc->image_views = nullptr;
    sc->images = nullptr;

    if (sc->swapchain)
    {
        vkDestroySwapchainKHR(ctx->device, sc->swapchain, nullptr);
        sc->swapchain = VK_NULL_HANDLE;
    }
}

bool mel_vk_swapchain_init(Mel_VkSwapchain* sc, Mel_VkContext* ctx, u32 width, u32 height)
{
    assert(sc != nullptr);
    assert(ctx != nullptr);

    *sc = (Mel_VkSwapchain){0};

    if (!create_swapchain(sc, ctx, width, height, VK_NULL_HANDLE)) return false;
    if (!create_image_views(sc, ctx)) return false;
    if (!create_command_pools(sc, ctx)) return false;
    if (!create_sync_objects(sc, ctx)) return false;

    return true;
}

void mel_vk_swapchain_shutdown(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    assert(sc != nullptr);
    assert(ctx != nullptr);

    vkDeviceWaitIdle(ctx->device);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(ctx->device, sc->image_available[i], nullptr);
        vkDestroySemaphore(ctx->device, sc->render_finished[i], nullptr);
        vkDestroyFence(ctx->device, sc->in_flight[i], nullptr);
        vkDestroyCommandPool(ctx->device, sc->command_pools[i], nullptr);
    }

    destroy_swapchain_resources(sc, ctx);
}

bool mel_vk_swapchain_recreate(Mel_VkSwapchain* sc, Mel_VkContext* ctx, u32 width, u32 height)
{
    vkDeviceWaitIdle(ctx->device);

    VkSwapchainKHR old = sc->swapchain;

    for (u32 i = 0; i < sc->image_count; i++)
    {
        vkDestroyImageView(ctx->device, sc->image_views[i], nullptr);
    }
    mel_dealloc(mel_alloc_heap(), sc->image_views);
    mel_dealloc(mel_alloc_heap(), sc->images);

    sc->swapchain = VK_NULL_HANDLE;
    sc->image_views = nullptr;
    sc->images = nullptr;

    if (!create_swapchain(sc, ctx, width, height, old)) return false;

    vkDestroySwapchainKHR(ctx->device, old, nullptr);

    if (!create_image_views(sc, ctx)) return false;

    return true;
}

bool mel_vk_swapchain_acquire(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    TracyCZoneN(ctx_wait_fence, "vkWaitForFences", true);
    vkWaitForFences(ctx->device, 1, &sc->in_flight[sc->current_frame], VK_TRUE, UINT64_MAX);
    TracyCZoneEnd(ctx_wait_fence);

    TracyCZoneN(ctx_acquire_img, "vkAcquireNextImageKHR", true);
    VkResult result = vkAcquireNextImageKHR(
        ctx->device,
        sc->swapchain,
        UINT64_MAX,
        sc->image_available[sc->current_frame],
        VK_NULL_HANDLE,
        &sc->current_image
    );
    TracyCZoneEnd(ctx_acquire_img);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return false;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        SDL_Log("vkAcquireNextImageKHR failed with result: %d", (int)result);
    }
    assert(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);

    vkResetFences(ctx->device, 1, &sc->in_flight[sc->current_frame]);

    return true;
}

bool mel_vk_swapchain_present(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sc->render_finished[sc->current_frame],
        .swapchainCount = 1,
        .pSwapchains = &sc->swapchain,
        .pImageIndices = &sc->current_image,
    };

    TracyCZoneN(ctx_present_khr, "vkQueuePresentKHR", true);
    VkResult result = vkQueuePresentKHR(ctx->present_queue, &present_info);
    TracyCZoneEnd(ctx_present_khr);

    sc->current_frame = (sc->current_frame + 1) % MEL_MAX_FRAMES_IN_FLIGHT;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        return false;
    }

    assert(result == VK_SUCCESS);
    return true;
}

VkSemaphore mel_vk_swapchain_image_available(Mel_VkSwapchain* sc)
{
    return sc->image_available[sc->current_frame];
}

VkSemaphore mel_vk_swapchain_render_finished(Mel_VkSwapchain* sc)
{
    return sc->render_finished[sc->current_frame];
}

VkFence mel_vk_swapchain_in_flight_fence(Mel_VkSwapchain* sc)
{
    return sc->in_flight[sc->current_frame];
}

VkCommandBuffer mel_vk_swapchain_command_buffer(Mel_VkSwapchain* sc)
{
    return sc->command_buffers[sc->current_frame];
}

void mel_vk_swapchain_begin_frame(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    TracyCZoneN(ctx_zone, "vk_begin_frame", true);
    VkCommandBuffer cmd = sc->command_buffers[sc->current_frame];

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));

    MEL_UNUSED(ctx);
    TracyCZoneEnd(ctx_zone);
}

void mel_vk_swapchain_end_frame(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    VkCommandBuffer cmd = sc->command_buffers[sc->current_frame];

    TracyCZoneN(ctx_end_cmd, "vkEndCommandBuffer", true);
    VK_CHECK(vkEndCommandBuffer(cmd));
    TracyCZoneEnd(ctx_end_cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sc->image_available[sc->current_frame],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &sc->render_finished[sc->current_frame],
    };

    TracyCZoneN(ctx_submit, "vkQueueSubmit", true);
    VK_CHECK(vkQueueSubmit(ctx->graphics_queue, 1, &submit_info, sc->in_flight[sc->current_frame]));
    TracyCZoneEnd(ctx_submit);
}

void mel_vk_swapchain_clear(Mel_VkSwapchain* sc, Mel_VkContext* ctx, f32 r, f32 g, f32 b, f32 a)
{
    VkCommandBuffer cmd = sc->command_buffers[sc->current_frame];
    VkImage image = sc->images[sc->current_image];

    VkImageMemoryBarrier barrier_to_clear = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier_to_clear
    );

    VkClearColorValue clear_color = {{ r, g, b, a }};
    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);

    VkImageMemoryBarrier barrier_to_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier_to_present
    );

    MEL_UNUSED(ctx);
}

void mel_vk_swapchain_begin_rendering(Mel_VkSwapchain* sc, Mel_VkContext* ctx, f32 r, f32 g, f32 b, f32 a)
{
    TracyCZoneN(ctx_zone, "vk_begin_rendering", true);
    VkCommandBuffer cmd = sc->command_buffers[sc->current_frame];
    VkImage image = sc->images[sc->current_image];
    VkImageView view = sc->image_views[sc->current_image];

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    VkRenderingAttachmentInfo color_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.color = {{ r, g, b, a }},
    };

    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = {0, 0}, .extent = sc->extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
    };

    vkCmdBeginRendering(cmd, &rendering_info);

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (f32)sc->extent.width,
        .height = (f32)sc->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = sc->extent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    MEL_UNUSED(ctx);
    TracyCZoneEnd(ctx_zone);
}

void mel_vk_swapchain_end_rendering(Mel_VkSwapchain* sc, Mel_VkContext* ctx)
{
    TracyCZoneN(ctx_zone, "vk_end_rendering", true);
    VkCommandBuffer cmd = sc->command_buffers[sc->current_frame];
    VkImage image = sc->images[sc->current_image];

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    MEL_UNUSED(ctx);
    TracyCZoneEnd(ctx_zone);
}
