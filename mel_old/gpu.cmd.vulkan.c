#include "gpu.cmd.h"
#include "gpu.device.vulkan.h"
#include "gpu.types.vulkan.h"
#include "gpu.image.h"
#include "gpu.buffer.h"
#include "gpu.pipeline.h"

void mel_gpu_cmd_begin(Mel_Gpu_Cmd* c)
{
    assert(c != nullptr);
    assert(c->_cmd != nullptr);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult r = vkBeginCommandBuffer((VkCommandBuffer)c->_cmd, &begin_info);
    assert(r == VK_SUCCESS);
}

void mel_gpu_cmd_end(Mel_Gpu_Cmd* c)
{
    assert(c != nullptr);
    assert(c->_cmd != nullptr);

    VkResult r = vkEndCommandBuffer((VkCommandBuffer)c->_cmd);
    assert(r == VK_SUCCESS);
}

void mel_gpu_cmd_begin_rendering_opt(Mel_Gpu_Cmd* c, Mel_Gpu_Rendering_Opt opt)
{
    assert(c != nullptr);
    assert(c->_cmd != nullptr);
    assert(opt.color_count > 0 || opt.depth_attachment != nullptr);
    assert(opt.color_count == 0 || opt.color_attachments != nullptr);
    assert(opt.render_width > 0);
    assert(opt.render_height > 0);

    VkRenderingAttachmentInfo color_infos[8];
    assert(opt.color_count <= 8);

    for (u32 i = 0; i < opt.color_count; i++)
    {
        Mel_Gpu_Color_Attachment* src = &opt.color_attachments[i];
        color_infos[i] = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = (VkImageView)src->_image_view,
            .imageLayout = mel__gpu_image_layout_to_vk(src->layout ? src->layout : MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT),
            .loadOp = mel__gpu_load_op_to_vk(src->load_op),
            .storeOp = mel__gpu_store_op_to_vk(src->store_op ? src->store_op : MEL_GPU_STORE_OP_STORE),
            .clearValue.color = {{ src->clear_r, src->clear_g, src->clear_b, src->clear_a }},
        };
    }

    VkRenderingAttachmentInfo depth_info = {0};
    if (opt.depth_attachment)
    {
        Mel_Gpu_Depth_Attachment* d = opt.depth_attachment;
        depth_info = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = (VkImageView)d->_image_view,
            .imageLayout = mel__gpu_image_layout_to_vk(d->layout ? d->layout : MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT),
            .loadOp = mel__gpu_load_op_to_vk(d->load_op),
            .storeOp = mel__gpu_store_op_to_vk(d->store_op ? d->store_op : MEL_GPU_STORE_OP_STORE),
            .clearValue.depthStencil = { d->clear_depth, d->clear_stencil },
        };
    }

    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = {0, 0}, .extent = { opt.render_width, opt.render_height } },
        .layerCount = 1,
        .colorAttachmentCount = opt.color_count,
        .pColorAttachments = opt.color_count > 0 ? color_infos : nullptr,
        .pDepthAttachment = opt.depth_attachment ? &depth_info : nullptr,
    };

    vkCmdBeginRendering((VkCommandBuffer)c->_cmd, &rendering_info);
}

void mel_gpu_cmd_end_rendering(Mel_Gpu_Cmd* c)
{
    assert(c != nullptr);
    assert(c->_cmd != nullptr);
    vkCmdEndRendering((VkCommandBuffer)c->_cmd);
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline)
{
    assert(c != nullptr);
    assert(pipeline != nullptr);
    assert(pipeline->_pipeline != nullptr);
    VkPipelineBindPoint bind_point;
    switch (pipeline->_bind_point) {
        case MEL_GPU_PIPELINE_COMPUTE: bind_point = VK_PIPELINE_BIND_POINT_COMPUTE; break;
        default: bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS; break;
    }
    vkCmdBindPipeline((VkCommandBuffer)c->_cmd, bind_point, (VkPipeline)pipeline->_pipeline);
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, u64 offset)
{
    assert(c != nullptr);
    assert(buffer != nullptr);
    assert(buffer->_handle != nullptr);
    VkBuffer vk_buf = (VkBuffer)buffer->_handle;
    vkCmdBindVertexBuffers((VkCommandBuffer)c->_cmd, 0, 1, &vk_buf, &offset);
}

void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, u64 offset, Mel_Gpu_Index_Type type)
{
    assert(c != nullptr);
    assert(buffer != nullptr);
    assert(buffer->_handle != nullptr);
    vkCmdBindIndexBuffer((VkCommandBuffer)c->_cmd, (VkBuffer)buffer->_handle, offset, mel__gpu_index_type_to_vk(type));
}

void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline, void* set)
{
    assert(c != nullptr);
    assert(pipeline != nullptr);
    assert(pipeline->_layout != nullptr);
    VkDescriptorSet vk_set = (VkDescriptorSet)set;
    VkPipelineBindPoint bind_point;
    switch (pipeline->_bind_point) {
        case MEL_GPU_PIPELINE_COMPUTE: bind_point = VK_PIPELINE_BIND_POINT_COMPUTE; break;
        default: bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS; break;
    }
    vkCmdBindDescriptorSets((VkCommandBuffer)c->_cmd, bind_point,
                            (VkPipelineLayout)pipeline->_layout, 0, 1, &vk_set, 0, nullptr);
}

void mel_gpu_cmd_push_constants(Mel_Gpu_Cmd* c, Mel_Gpu_Pipeline* pipeline,
                                Mel_Gpu_Shader_Stage stages, u32 offset, u32 size, const void* data)
{
    assert(c != nullptr);
    assert(pipeline != nullptr);
    assert(pipeline->_layout != nullptr);
    assert(data != nullptr);
    assert(size > 0);
    vkCmdPushConstants((VkCommandBuffer)c->_cmd, (VkPipelineLayout)pipeline->_layout,
                       mel__gpu_shader_stage_to_vk(stages), offset, size, data);
}

void mel_gpu_cmd_set_viewport(Mel_Gpu_Cmd* c, f32 x, f32 y, f32 w, f32 h, f32 min_depth, f32 max_depth)
{
    assert(c != nullptr);
    VkViewport viewport = { x, y, w, h, min_depth, max_depth };
    vkCmdSetViewport((VkCommandBuffer)c->_cmd, 0, 1, &viewport);
}

void mel_gpu_cmd_set_scissor(Mel_Gpu_Cmd* c, i32 x, i32 y, u32 w, u32 h)
{
    assert(c != nullptr);
    VkRect2D scissor = { .offset = { x, y }, .extent = { w, h } };
    vkCmdSetScissor((VkCommandBuffer)c->_cmd, 0, 1, &scissor);
}

void mel_gpu_cmd_set_cull_mode(Mel_Gpu_Cmd* c, u32 cull_mode)
{
    assert(c != nullptr);
    VkCullModeFlags vk_cull;
    switch (cull_mode)
    {
        case MEL_GPU_CULL_BACK:  vk_cull = VK_CULL_MODE_BACK_BIT; break;
        case MEL_GPU_CULL_FRONT: vk_cull = VK_CULL_MODE_FRONT_BIT; break;
        default:                 vk_cull = VK_CULL_MODE_NONE; break;
    }
    vkCmdSetCullMode((VkCommandBuffer)c->_cmd, vk_cull);
}

void mel_gpu_cmd_draw(Mel_Gpu_Cmd* c, u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance)
{
    assert(c != nullptr);
    vkCmdDraw((VkCommandBuffer)c->_cmd, vertex_count, instance_count, first_vertex, first_instance);
}

void mel_gpu_cmd_draw_indexed(Mel_Gpu_Cmd* c, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance)
{
    assert(c != nullptr);
    vkCmdDrawIndexed((VkCommandBuffer)c->_cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void mel_gpu_cmd_draw_indexed_indirect(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, u64 offset, u32 draw_count, u32 stride)
{
    assert(c != nullptr);
    assert(buffer != nullptr);
    assert(buffer->_handle != nullptr);
    assert(draw_count > 0);
    if (c->dev && !c->dev->capabilities.multi_draw_indirect && draw_count > 1)
    {
        u64 draw_stride = stride ? stride : sizeof(VkDrawIndexedIndirectCommand);
        for (u32 i = 0; i < draw_count; i++)
            vkCmdDrawIndexedIndirect((VkCommandBuffer)c->_cmd, (VkBuffer)buffer->_handle, offset + draw_stride * i, 1, (u32)draw_stride);
        return;
    }

    vkCmdDrawIndexedIndirect((VkCommandBuffer)c->_cmd, (VkBuffer)buffer->_handle, offset, draw_count, stride);
}

void mel_gpu_cmd_dispatch(Mel_Gpu_Cmd* c, u32 group_count_x, u32 group_count_y, u32 group_count_z)
{
    assert(c != nullptr);
    vkCmdDispatch((VkCommandBuffer)c->_cmd, group_count_x, group_count_y, group_count_z);
}

void mel_gpu_cmd_draw_mesh_tasks(Mel_Gpu_Cmd* c, u32 group_count_x, u32 group_count_y, u32 group_count_z)
{
    assert(c != nullptr);
    assert(c->dev != nullptr);
    assert(c->dev->capabilities.mesh_shader);
    vkCmdDrawMeshTasksEXT((VkCommandBuffer)c->_cmd, group_count_x, group_count_y, group_count_z);
}

void mel_gpu_cmd_draw_mesh_tasks_indirect(Mel_Gpu_Cmd* c, Mel_Gpu_Buffer* buffer, u64 offset, u32 draw_count, u32 stride)
{
    assert(c != nullptr);
    assert(c->dev != nullptr);
    assert(c->dev->capabilities.mesh_shader);
    assert(buffer != nullptr);
    assert(buffer->_handle != nullptr);
    assert(draw_count > 0);
    vkCmdDrawMeshTasksIndirectEXT((VkCommandBuffer)c->_cmd, (VkBuffer)buffer->_handle, offset, draw_count,
        stride ? stride : (u32)sizeof(VkDrawMeshTasksIndirectCommandEXT));
}

void mel_gpu_cmd_image_barrier(Mel_Gpu_Cmd* c,
                               void* image,
                               Mel_Gpu_Stage src_stage, Mel_Gpu_Access src_access,
                               Mel_Gpu_Stage dst_stage, Mel_Gpu_Access dst_access,
                               Mel_Gpu_Image_Layout old_layout, Mel_Gpu_Image_Layout new_layout,
                               Mel_Gpu_Aspect aspect)
{
    assert(c != nullptr);
    assert(image != nullptr);

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = mel__gpu_stage_to_vk(src_stage),
        .srcAccessMask = mel__gpu_access_to_vk(src_access),
        .dstStageMask = mel__gpu_stage_to_vk(dst_stage),
        .dstAccessMask = mel__gpu_access_to_vk(dst_access),
        .oldLayout = mel__gpu_image_layout_to_vk(old_layout),
        .newLayout = mel__gpu_image_layout_to_vk(new_layout),
        .image = (VkImage)image,
        .subresourceRange = { mel__gpu_aspect_to_vk(aspect), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS },
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2((VkCommandBuffer)c->_cmd, &dep);
}

void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Cmd* c,
                                Mel_Gpu_Buffer* buffer,
                                Mel_Gpu_Stage src_stage, Mel_Gpu_Access src_access,
                                Mel_Gpu_Stage dst_stage, Mel_Gpu_Access dst_access)
{
    assert(c != nullptr);
    assert(buffer != nullptr);
    assert(buffer->_handle != nullptr);

    VkBufferMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = mel__gpu_stage_to_vk(src_stage),
        .srcAccessMask = mel__gpu_access_to_vk(src_access),
        .dstStageMask = mel__gpu_stage_to_vk(dst_stage),
        .dstAccessMask = mel__gpu_access_to_vk(dst_access),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = (VkBuffer)buffer->_handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2((VkCommandBuffer)c->_cmd, &dep);
}

void mel_gpu_cmd_transition_image(Mel_Gpu_Cmd* c, Mel_Gpu_Image* image, Mel_Gpu_Image_Layout new_layout)
{
    assert(c != nullptr);
    assert(image != nullptr);
    mel_gpu_image_transition(image, c, new_layout);
}
