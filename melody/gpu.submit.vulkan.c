#include "gpu.submit.h"
#include "gpu.device.vulkan.h"
#include "gpu.cmd.h"
#include "swapchain.h"
#include <tracy/TracyC.h>

typedef struct {
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    bool initialized;
} Gpu_Immediate_Ctx;

static Gpu_Immediate_Ctx s_immediate = {0};

static void ensure_immediate(Mel_Gpu_Device* dev)
{
    if (s_immediate.initialized) return;

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = dev->graphics_family,
    };

    VkResult r = vkCreateCommandPool(mel__gpu_device_vk(dev)->device, &pool_info, nullptr, &s_immediate.pool);
    assert(r == VK_SUCCESS);

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s_immediate.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    r = vkAllocateCommandBuffers(mel__gpu_device_vk(dev)->device, &alloc_info, &s_immediate.cmd);
    assert(r == VK_SUCCESS);

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    r = vkCreateFence(mel__gpu_device_vk(dev)->device, &fence_info, nullptr, &s_immediate.fence);
    assert(r == VK_SUCCESS);

    s_immediate.initialized = true;
}

void mel_gpu_submit_immediate(Mel_Gpu_Device* dev, Mel_Gpu_Submit_Fn callback, void* user)
{
    assert(dev != nullptr);
    assert(callback != nullptr);

    TracyCZoneN(ctx, "gpu_submit_immediate", true);

    ensure_immediate(dev);

    vkResetCommandBuffer(s_immediate.cmd, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(s_immediate.cmd, &begin_info);

    Mel_Gpu_Cmd wrap = { ._cmd = s_immediate.cmd };
    callback(&wrap, user);

    vkEndCommandBuffer(s_immediate.cmd);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &s_immediate.cmd,
    };

    vkResetFences(mel__gpu_device_vk(dev)->device, 1, &s_immediate.fence);
    vkQueueSubmit(mel__gpu_device_vk(dev)->graphics_queue, 1, &submit_info, s_immediate.fence);

    TracyCZoneN(ctx_wait, "gpu_fence_wait", true);
    vkWaitForFences(mel__gpu_device_vk(dev)->device, 1, &s_immediate.fence, VK_TRUE, UINT64_MAX);
    TracyCZoneEnd(ctx_wait);

    TracyCZoneEnd(ctx);
}

void mel_gpu_submit_frame_opt(Mel_Gpu_Device* dev, Mel_Gpu_Submit_Frame_Opt opt)
{
    assert(dev != nullptr);
    assert(opt.callback != nullptr);
    assert(opt.swapchain != nullptr);

    TracyCZoneN(ctx, "gpu_submit_frame", true);

    ensure_immediate(dev);

    vkResetCommandBuffer(s_immediate.cmd, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(s_immediate.cmd, &begin_info);

    VkImage swapchain_image = (VkImage)opt.swapchain->_images[opt.swapchain->current_image];
    VkImageMemoryBarrier2 to_color_att = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = swapchain_image,
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
        .pImageMemoryBarriers = &to_color_att,
    };

    vkCmdPipelineBarrier2(s_immediate.cmd, &dep);

    Mel_Gpu_Cmd wrap = { ._cmd = s_immediate.cmd, .dev = dev };
    opt.callback(&wrap, opt.user);

    mel_swapchain_prepare_present(opt.swapchain, &wrap);

    vkEndCommandBuffer(s_immediate.cmd);

    Mel_Gpu_Submit_Gather gather = {0};
    mel_swapchain_collect_sync(opt.swapchain, &gather);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = gather.wait_count,
        .pWaitSemaphores      = (VkSemaphore*)gather._wait_semaphores,
        .pWaitDstStageMask    = gather._wait_stages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &s_immediate.cmd,
        .signalSemaphoreCount = gather.signal_count,
        .pSignalSemaphores    = (VkSemaphore*)gather._signal_semaphores,
    };

    vkResetFences(mel__gpu_device_vk(dev)->device, 1, &s_immediate.fence);
    vkQueueSubmit(mel__gpu_device_vk(dev)->graphics_queue, 1, &submit_info, s_immediate.fence);

    TracyCZoneN(ctx_wait, "gpu_frame_fence_wait", true);
    vkWaitForFences(mel__gpu_device_vk(dev)->device, 1, &s_immediate.fence, VK_TRUE, UINT64_MAX);
    TracyCZoneEnd(ctx_wait);

    TracyCZoneEnd(ctx);
}

void mel_gpu_submit_shutdown(Mel_Gpu_Device* dev)
{
    if (!s_immediate.initialized) return;

    vkDestroyFence(mel__gpu_device_vk(dev)->device, s_immediate.fence, nullptr);
    vkDestroyCommandPool(mel__gpu_device_vk(dev)->device, s_immediate.pool, nullptr);
    s_immediate = (Gpu_Immediate_Ctx){0};
}
