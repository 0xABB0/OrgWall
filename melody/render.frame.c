#define VK_NO_PROTOTYPES
#include "render.frame.h"
#include "swapchain.h"
#include "gpu.cmd.h"
#include <tracy/TracyC.h>

bool mel_render_frame_init_opt(Mel_Render_Frame* rf, Mel_Render_Frame_Opt opt)
{
    assert(rf != nullptr);
    assert(opt.dev != nullptr);
    assert(opt.swapchain != nullptr);

    *rf = (Mel_Render_Frame){0};
    rf->dev = opt.dev;
    rf->swapchain = opt.swapchain;
    rf->frame_count = opt.frame_count > 0 ? opt.frame_count : 2;
    assert(rf->frame_count <= MEL_MAX_FRAMES_IN_FLIGHT);

    for (u32 i = 0; i < rf->frame_count; i++)
    {
        Mel_Render_Frame_Data* fd = &rf->frames[i];

        VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = rf->dev->graphics_family,
        };
        VkResult r = vkCreateCommandPool(rf->dev->device, &pool_info, nullptr, &fd->command_pool);
        if (r != VK_SUCCESS)
        {
            SDL_Log("Failed to create command pool for frame %u: %d", i, r);
            goto fail;
        }

        VkCommandBufferAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = fd->command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        r = vkAllocateCommandBuffers(rf->dev->device, &alloc_info, &fd->command_buffer);
        if (r != VK_SUCCESS)
        {
            SDL_Log("Failed to allocate command buffer for frame %u: %d", i, r);
            goto fail;
        }

        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        r = vkCreateFence(rf->dev->device, &fence_info, nullptr, &fd->in_flight);
        if (r != VK_SUCCESS)
        {
            SDL_Log("Failed to create in_flight fence for frame %u: %d", i, r);
            goto fail;
        }
    }

    return true;

fail:
    mel_render_frame_shutdown(rf);
    return false;
}

void mel_render_frame_shutdown(Mel_Render_Frame* rf)
{
    assert(rf != nullptr);
    assert(rf->dev != nullptr);

    for (u32 i = 0; i < rf->frame_count; i++)
    {
        Mel_Render_Frame_Data* fd = &rf->frames[i];
        vkDestroyFence(rf->dev->device, fd->in_flight, nullptr);
        vkDestroyCommandPool(rf->dev->device, fd->command_pool, nullptr);
    }
}

bool mel_render_frame_begin(Mel_Render_Frame* rf)
{
    assert(rf != nullptr);

    Mel_Render_Frame_Data* fd = &rf->frames[rf->current_frame];

    TracyCZoneN(ctx_fence, "frame_fence_wait", true);
    vkWaitForFences(rf->dev->device, 1, &fd->in_flight, VK_TRUE, UINT64_MAX);
    TracyCZoneEnd(ctx_fence);

    TracyCZoneN(ctx_acquire, "swapchain_acquire", true);
    if (!mel_swapchain_acquire(rf->swapchain, rf->dev))
    {
        TracyCZoneEnd(ctx_acquire);
        return false;
    }
    TracyCZoneEnd(ctx_acquire);

    rf->current_image = rf->swapchain->current_image;
    vkResetFences(rf->dev->device, 1, &fd->in_flight);
    vkResetCommandPool(rf->dev->device, fd->command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VkResult r = vkBeginCommandBuffer(fd->command_buffer, &begin_info);
    assert(r == VK_SUCCESS);

    return true;
}

void mel_render_frame_end(Mel_Render_Frame* rf)
{
    assert(rf != nullptr);

    Mel_Render_Frame_Data* fd = &rf->frames[rf->current_frame];

    TracyCZoneN(ctx_present, "swapchain_present", true);
    mel_swapchain_present(rf->swapchain, rf->dev, fd->command_buffer, fd->in_flight);
    TracyCZoneEnd(ctx_present);

    rf->current_frame = (rf->current_frame + 1) % rf->frame_count;
}

VkCommandBuffer mel_render_frame_cmd(Mel_Render_Frame* rf)
{
    assert(rf != nullptr);
    return rf->frames[rf->current_frame].command_buffer;
}

Mel_Render_Frame_Data* mel_render_frame_current(Mel_Render_Frame* rf)
{
    assert(rf != nullptr);
    return &rf->frames[rf->current_frame];
}
