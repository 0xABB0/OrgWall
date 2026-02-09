#define VK_NO_PROTOTYPES
#include "gpu.cmd.h"
#include "gpu.image.h"
#include "gpu.buffer.h"
#include "gpu.pipeline.h"

void mel_gpu_cmd_begin(Mel_Gpu_Cmd* c)
{
    assert(c != nullptr);
    assert(c->cmd != VK_NULL_HANDLE);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult r = vkBeginCommandBuffer(c->cmd, &begin_info);
    assert(r == VK_SUCCESS);
}

void mel_gpu_cmd_end(Mel_Gpu_Cmd* c)
{
    assert(c != nullptr);
    assert(c->cmd != VK_NULL_HANDLE);

    VkResult r = vkEndCommandBuffer(c->cmd);
    assert(r == VK_SUCCESS);
}

void mel_gpu_cmd_begin_rendering_opt(Mel_Gpu_Cmd* c, Mel_Gpu_Rendering_Opt opt)
{
    assert(c != nullptr);
    assert(c->cmd != VK_NULL_HANDLE);
    assert(opt.color_count > 0);
    assert(opt.color_attachments != nullptr);
    assert(opt.render_width > 0);
    assert(opt.render_height > 0);

    VkRenderingAttachmentInfo attachments[8];
    assert(opt.color_count <= 8);

    for (u32 i = 0; i < opt.color_count; i++)
    {
        Mel_Gpu_Color_Attachment* src = &opt.color_attachments[i];
        attachments[i] = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = src->image_view,
            .imageLayout = src->layout ? src->layout : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = src->load_op,
            .storeOp = src->store_op ? src->store_op : VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue.color = {{ src->clear_r, src->clear_g, src->clear_b, src->clear_a }},
        };
    }

    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = {0, 0}, .extent = { opt.render_width, opt.render_height } },
        .layerCount = 1,
        .colorAttachmentCount = opt.color_count,
        .pColorAttachments = attachments,
    };

    vkCmdBeginRendering(c->cmd, &rendering_info);
}

void mel_gpu_cmd_end_rendering(Mel_Gpu_Cmd* c)
{
    assert(c != nullptr);
    assert(c->cmd != VK_NULL_HANDLE);
    vkCmdEndRendering(c->cmd);
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline)
{
    assert(c != nullptr);
    assert(pipeline != nullptr);
    assert(pipeline->pipeline != VK_NULL_HANDLE);
    vkCmdBindPipeline(c->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, VkDeviceSize offset)
{
    assert(c != nullptr);
    assert(buffer != nullptr);
    assert(buffer->buffer != VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(c->cmd, 0, 1, &buffer->buffer, &offset);
}

void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, VkDeviceSize offset, VkIndexType type)
{
    assert(c != nullptr);
    assert(buffer != nullptr);
    assert(buffer->buffer != VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(c->cmd, buffer->buffer, offset, type);
}

void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline, VkDescriptorSet set)
{
    assert(c != nullptr);
    assert(pipeline != nullptr);
    assert(pipeline->layout != VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(c->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->layout, 0, 1, &set, 0, nullptr);
}

void mel_gpu_cmd_push_constants(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline,
                                VkShaderStageFlags stages, u32 offset, u32 size, const void* data)
{
    assert(c != nullptr);
    assert(pipeline != nullptr);
    assert(pipeline->layout != VK_NULL_HANDLE);
    assert(data != nullptr);
    assert(size > 0);
    vkCmdPushConstants(c->cmd, pipeline->layout, stages, offset, size, data);
}

void mel_gpu_cmd_set_viewport(Mel_Gpu_Cmd* c, f32 x, f32 y, f32 w, f32 h, f32 min_depth, f32 max_depth)
{
    assert(c != nullptr);
    VkViewport viewport = { x, y, w, h, min_depth, max_depth };
    vkCmdSetViewport(c->cmd, 0, 1, &viewport);
}

void mel_gpu_cmd_set_scissor(Mel_Gpu_Cmd* c, i32 x, i32 y, u32 w, u32 h)
{
    assert(c != nullptr);
    VkRect2D scissor = { .offset = { x, y }, .extent = { w, h } };
    vkCmdSetScissor(c->cmd, 0, 1, &scissor);
}

void mel_gpu_cmd_draw(Mel_Gpu_Cmd* c, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance)
{
    assert(c != nullptr);
    vkCmdDraw(c->cmd, vertex_count, instance_count, first_vertex, first_instance);
}

void mel_gpu_cmd_draw_indexed(Mel_Gpu_Cmd* c, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance)
{
    assert(c != nullptr);
    vkCmdDrawIndexed(c->cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void mel_gpu_cmd_image_barrier(Mel_Gpu_Cmd* c,
                               VkImage image,
                               VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                               VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                               VkImageLayout old_layout, VkImageLayout new_layout,
                               VkImageAspectFlags aspect)
{
    assert(c != nullptr);
    assert(image != VK_NULL_HANDLE);

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .image = image,
        .subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS },
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(c->cmd, &dep);
}

void mel_gpu_cmd_transition_image(Mel_Gpu_Cmd* c, Mel_Gpu_Image* image, VkImageLayout new_layout)
{
    assert(c != nullptr);
    assert(image != nullptr);
    mel_gpu_image_transition(image, c->cmd, new_layout);
}
