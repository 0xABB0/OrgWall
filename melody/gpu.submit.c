#define VK_NO_PROTOTYPES
#include "gpu.submit.h"

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

    VkResult r = vkCreateCommandPool(dev->device, &pool_info, nullptr, &s_immediate.pool);
    assert(r == VK_SUCCESS);

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s_immediate.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    r = vkAllocateCommandBuffers(dev->device, &alloc_info, &s_immediate.cmd);
    assert(r == VK_SUCCESS);

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    r = vkCreateFence(dev->device, &fence_info, nullptr, &s_immediate.fence);
    assert(r == VK_SUCCESS);

    s_immediate.initialized = true;
}

void mel_gpu_submit_immediate(Mel_Gpu_Device* dev, Mel_Gpu_Submit_Fn callback, void* user)
{
    assert(dev != nullptr);
    assert(callback != nullptr);

    ensure_immediate(dev);

    vkResetCommandBuffer(s_immediate.cmd, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(s_immediate.cmd, &begin_info);
    callback(s_immediate.cmd, user);
    vkEndCommandBuffer(s_immediate.cmd);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &s_immediate.cmd,
    };

    vkResetFences(dev->device, 1, &s_immediate.fence);
    vkQueueSubmit(dev->graphics_queue, 1, &submit_info, s_immediate.fence);
    vkWaitForFences(dev->device, 1, &s_immediate.fence, VK_TRUE, UINT64_MAX);
}
