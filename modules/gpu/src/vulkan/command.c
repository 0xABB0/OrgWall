#include "vulkan_backend.h"

void mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc)
{
    if (!sc) return;
    VkDevice d = sc->device->device;

    vkWaitForFences(d, 1, &sc->in_flight, VK_TRUE, UINT64_MAX);

    VkResult acq = vkAcquireNextImageKHR(d, sc->swapchain, UINT64_MAX,
                                         sc->image_available, VK_NULL_HANDLE, &sc->current_image);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        mel_gpu_swapchain_resize(sc, sc->req_width, sc->req_height);
        sc->frame_ok = false;
        return;
    }

    vkResetFences(d, 1, &sc->in_flight);
    vkResetCommandBuffer(sc->cmd_buffer, 0);

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(sc->cmd_buffer, &bi);
    sc->cmd.cb   = sc->cmd_buffer;
    sc->frame_ok = true;
}

Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc)
{
    return sc ? &sc->cmd : NULL;
}

void mel_gpu_frame_end(Mel_Gpu_Swapchain* sc)
{
    if (!sc || !sc->frame_ok) return;
    vkEndCommandBuffer(sc->cmd_buffer);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &sc->image_available,
        .pWaitDstStageMask    = &wait_stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &sc->cmd_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &sc->render_finished,
    };
    vkQueueSubmit(sc->device->queue, 1, &si, sc->in_flight);

    VkPresentInfoKHR pi = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &sc->render_finished,
        .swapchainCount     = 1,
        .pSwapchains        = &sc->swapchain,
        .pImageIndices      = &sc->current_image,
    };
    VkResult pr = vkQueuePresentKHR(sc->device->queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        mel_gpu_swapchain_resize(sc, sc->req_width, sc->req_height);
    }
    sc->frame_ok = false;
}

void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear)
{
    if (!cmd || !cmd->swapchain->frame_ok) return;
    Mel_Gpu_Swapchain* sc = cmd->swapchain;

    VkClearValue clear_value = { .color = { .float32 = { clear.r, clear.g, clear.b, clear.a } } };
    VkRenderPassBeginInfo rp = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = sc->render_pass,
        .framebuffer     = sc->framebuffers[sc->current_image],
        .renderArea      = { { 0, 0 }, sc->extent },
        .clearValueCount = 1,
        .pClearValues    = &clear_value,
    };
    vkCmdBeginRenderPass(cmd->cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Negative-height viewport flips Vulkan's +Y-down clip space to match the
    // +Y-up convention the Metal and WebGPU backends use, so geometry is oriented
    // identically across all three.
    VkViewport vp = { 0.0f, (float)sc->extent.height, (float)sc->extent.width,
                      -(float)sc->extent.height, 0.0f, 1.0f };
    VkRect2D   scissor = { { 0, 0 }, sc->extent };
    vkCmdSetViewport(cmd->cb, 0, 1, &vp);
    vkCmdSetScissor(cmd->cb, 0, 1, &scissor);
}

void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd)
{
    if (!cmd || !cmd->swapchain->frame_ok) return;
    vkCmdEndRenderPass(cmd->cb);
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline* pipe)
{
    if (!cmd || !cmd->swapchain->frame_ok || !pipe) return;
    vkCmdBindPipeline(cmd->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->pipeline);
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer* buf)
{
    if (!cmd || !cmd->swapchain->frame_ok || !buf) return;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd->cb, slot, 1, &buf->buf, &offset);
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count)
{
    if (!cmd || !cmd->swapchain->frame_ok) return;
    vkCmdDraw(cmd->cb, vertex_count, instance_count, 0, 0);
}
