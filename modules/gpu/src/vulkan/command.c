#include "vk_backend.h"

#include <log/log.h>

void mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc)
{
    Mel_Gpu_Device* dev = sc->dev;
    u32             frame = sc->frame_index;
    sc->frame_ok = false;

    vkWaitForFences(dev->vk, 1, &sc->in_flight[frame], VK_TRUE, UINT64_MAX);

    // This frame slot's prior submission has now completed: advance the retirement watermark past it.
    if (sc->frame_serial[frame])
        mel_gpu__submit_complete(dev, sc->frame_serial[frame]);

    u32      image = 0;
    VkResult r = vkAcquireNextImageKHR(dev->vk, sc->vk, UINT64_MAX, sc->image_available[frame], VK_NULL_HANDLE, &image);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        mel_gpu_swapchain_resize(sc, (i32)sc->extent.width, (i32)sc->extent.height);
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    {
        mel_gpu__device_is_lost(dev, r, "vkAcquireNextImageKHR");
        return;
    }

    vkResetFences(dev->vk, 1, &sc->in_flight[frame]);

    VkCommandBuffer cb = sc->cmd_buffers[frame];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cb, &bi);

    sc->current_image = image;
    sc->recorder.cb = cb;
    sc->recorder.cur_layout = VK_NULL_HANDLE;
    sc->frame_ok = true;
}

Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc) { return &sc->recorder; }

void mel_gpu_frame_end(Mel_Gpu_Swapchain* sc)
{
    if (!sc->frame_ok)
        return;
    Mel_Gpu_Device* dev = sc->dev;
    u32             frame = sc->frame_index;
    VkCommandBuffer cb = sc->cmd_buffers[frame];

    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo         si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sc->image_available[frame],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &sc->render_finished[sc->current_image],
    };
    u64 serial = mel_gpu__submit_serial_next(dev);
    mel_mutex_lock(&dev->submit_lock);
    VkResult sr = vkQueueSubmit(dev->graphics_queue, 1, &si, sc->in_flight[frame]);
    mel_mutex_unlock(&dev->submit_lock);
    if (sr != VK_SUCCESS && mel_gpu__device_is_lost(dev, sr, "vkQueueSubmit"))
        return;
    sc->frame_serial[frame] = serial;

    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sc->render_finished[sc->current_image],
        .swapchainCount = 1,
        .pSwapchains = &sc->vk,
        .pImageIndices = &sc->current_image,
    };
    mel_mutex_lock(&dev->submit_lock);
    VkResult pr = vkQueuePresentKHR(dev->graphics_queue, &pi);
    mel_mutex_unlock(&dev->submit_lock);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
        mel_gpu_swapchain_resize(sc, (i32)sc->extent.width, (i32)sc->extent.height);
    else if (pr != VK_SUCCESS)
        mel_gpu__device_is_lost(dev, pr, "vkQueuePresentKHR");

    sc->frame_index = (sc->frame_index + 1) % sc->frames_in_flight;
}

void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear)
{
    Mel_Gpu_Swapchain* sc = cmd->sc;
    Mel_Gpu_Device*    dev = cmd->dev;

    if (dev->dynamic_rendering)
    {
        // U16: bring the freshly-acquired swapchain image to COLOR_ATTACHMENT and render dynamically.
        VkImageMemoryBarrier to_color = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sc->images[sc->current_image],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        vkCmdPipelineBarrier(cmd->cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &to_color);

        VkRenderingAttachmentInfoKHR color = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView = sc->views[sc->current_image],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { .float32 = { clear.r, clear.g, clear.b, clear.a } } },
        };
        VkRenderingInfoKHR ri = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
            .renderArea = { { 0, 0 }, sc->extent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color,
        };
        dev->cmd_begin_rendering(cmd->cb, &ri);
    }
    else
    {
        VkClearValue          cv = { .color = { .float32 = { clear.r, clear.g, clear.b, clear.a } } };
        VkRenderPassBeginInfo bi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = sc->render_pass,
            .framebuffer = sc->framebuffers[sc->current_image],
            .renderArea = { .offset = { 0, 0 }, .extent = sc->extent },
            .clearValueCount = 1,
            .pClearValues = &cv,
        };
        vkCmdBeginRenderPass(cmd->cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    }

    VkViewport vp = {
        .x = 0.0f,
        .y = (f32)sc->extent.height,
        .width = (f32)sc->extent.width,
        .height = -(f32)sc->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .offset = { 0, 0 }, .extent = sc->extent };
    vkCmdSetViewport(cmd->cb, 0, 1, &vp);
    vkCmdSetScissor(cmd->cb, 0, 1, &scissor);
}

void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd)
{
    Mel_Gpu_Device* dev = cmd->dev;
    if (dev->dynamic_rendering)
    {
        Mel_Gpu_Swapchain* sc = cmd->sc;
        dev->cmd_end_rendering(cmd->cb);
        VkImageMemoryBarrier to_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sc->images[sc->current_image],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        vkCmdPipelineBarrier(cmd->cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &to_present);
    }
    else
    {
        vkCmdEndRenderPass(cmd->cb);
    }
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline pipe)
{
    Mel_Gpu_Pipeline_Obj* o = mel_gpu__pipeline_obj(cmd->dev, pipe);
    if (!o)
    {
        mel_assert(!"bind_pipeline: invalid pipeline handle");
        return;
    }
    vkCmdBindPipeline(cmd->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, o->pipeline);
    cmd->cur_layout = o->layout;
    // U14: a bindless pipeline reads the device heap at set 0; bind it so the simple path (bind a pipeline,
    // push the root record, draw) just works (MEL-ENGINE-II). The explicit cmd_bind_bindless is the P2 peer.
    if (o->bindless && cmd->dev->bindless.enabled)
        vkCmdBindDescriptorSets(cmd->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, o->layout, 0, 1, &cmd->dev->bindless.set, 0, NULL);
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer buf)
{
    VkBuffer vb;
    if (!mel_gpu__buffer_get(cmd->dev, buf, &vb))
    {
        mel_assert(!"bind_vertex_buffer: invalid buffer handle");
        return;
    }
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd->cb, slot, 1, &vb, &offset);
}

void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Index_Type type)
{
    VkBuffer ib;
    if (!mel_gpu__buffer_get(cmd->dev, buf, &ib))
    {
        mel_assert(!"bind_index_buffer: invalid buffer handle");
        return;
    }
    vkCmdBindIndexBuffer(cmd->cb, ib, 0, type == MEL_GPU_INDEX_UINT32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

void mel_gpu_cmd_push_constants(Mel_Gpu_Command_List* cmd, u32 offset, u32 bytes, const void* data)
{
    mel_assert(cmd->cur_layout != VK_NULL_HANDLE);
    vkCmdPushConstants(cmd->cb, cmd->cur_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, offset, bytes, data);
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count) { vkCmdDraw(cmd->cb, vertex_count, instance_count, 0, 0); }

void mel_gpu_cmd_draw_indexed(Mel_Gpu_Command_List* cmd, u32 index_count, u32 instance_count) { vkCmdDrawIndexed(cmd->cb, index_count, instance_count, 0, 0, 0); }
