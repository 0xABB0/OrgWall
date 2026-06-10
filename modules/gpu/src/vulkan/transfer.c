#include "vk_backend.h"

#include <log/log.h>

#include <string.h>

typedef struct
{
    Mel_Gpu_Device*       dev;
    Mel_Gpu_Command_List* cmd;
    Mel_Gpu_Buffer        staging;
    Mel_Gpu_Future*       result_future;
} Mel_Gpu_Transfer_Ctx;

static void mel_gpu__transfer_complete(Mel_Gpu_Future* submit_future, void* user)
{
    Mel_Gpu_Transfer_Ctx* c = user;
    Mel_Gpu_Device*       dev = c->dev;

    u32 status = mel_gpu_future_status(submit_future);
    mel_gpu_future_resolve(c->result_future, NULL, mel_gpu_failed(status) ? MEL_GPU_TRANSFER_BACKEND_FAILED : MEL_GPU_TRANSFER_OK);

    mel_gpu_future_destroy(submit_future);
    mel_gpu_command_list_destroy(c->cmd);
    mel_gpu_buffer_destroy(dev, c->staging);
    mel_dealloc(dev->alloc, c);
}

static Mel_Gpu_Future* mel_gpu__transfer_fail(Mel_Gpu_Device* dev, u32 status)
{
    Mel_Gpu_Future* f = mel_gpu_future_create(dev ? dev->pump : NULL, dev ? dev->vat : NULL);
    mel_gpu_future_resolve(f, NULL, status);
    return f;
}

static Mel_Gpu_Future* mel_gpu__transfer_dispatch(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer staging)
{
    Mel_Gpu_Transfer_Ctx* c = mel_alloc_type(dev->alloc, Mel_Gpu_Transfer_Ctx);
    *c = (Mel_Gpu_Transfer_Ctx){ .dev = dev, .cmd = cmd, .staging = staging };
    c->result_future = mel_gpu_future_create(dev->pump, dev->vat);
    Mel_Gpu_Future* result_future = c->result_future;

    Mel_Gpu_Future* submit_future = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });

    if (mel_gpu_future_resolved(submit_future))
        mel_gpu__transfer_complete(submit_future, c);
    else
        mel_gpu_future_then(submit_future, mel_gpu__transfer_complete, c);

    return result_future;
}

Mel_Gpu_Future* mel_gpu_buffer_upload_async(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Buffer dst, usize dst_offset, const void* data, usize size)
{
    if (!dev || !q || !data || size == 0)
    {
        mel_log_error("gpu", "buffer_upload_async: invalid arguments (dev/queue/data/size)");
        return mel_gpu__transfer_fail(dev, MEL_GPU_TRANSFER_BAD_PARAMS);
    }
    VkBuffer dst_vk = VK_NULL_HANDLE;
    if (!mel_gpu__buffer_get(dev, dst, &dst_vk))
    {
        mel_log_error("gpu", "buffer_upload_async: invalid destination buffer handle");
        return mel_gpu__transfer_fail(dev, MEL_GPU_TRANSFER_BAD_PARAMS);
    }

    Mel_Gpu_Buffer_Create_Result st = mel_gpu_buffer_create(dev, .size = size, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_UPLOAD, .data = data, .name = "transfer-staging");
    if (mel_gpu_failed(st.status))
    {
        mel_log_error("gpu", "buffer_upload_async: staging buffer create failed");
        return mel_gpu__transfer_fail(dev, MEL_GPU_TRANSFER_BACKEND_FAILED);
    }
    VkBuffer staging_vk = VK_NULL_HANDLE;
    mel_gpu__buffer_get(dev, st.value, &staging_vk);

    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    if (!cmd)
    {
        mel_log_error("gpu", "buffer_upload_async: command list create failed");
        mel_gpu_buffer_destroy(dev, st.value);
        return mel_gpu__transfer_fail(dev, MEL_GPU_TRANSFER_BACKEND_FAILED);
    }

    mel_gpu_command_list_begin(cmd);
    VkBufferCopy region = { .srcOffset = 0, .dstOffset = (VkDeviceSize)dst_offset, .size = (VkDeviceSize)size };
    vkCmdCopyBuffer(cmd->cb, staging_vk, dst_vk, 1, &region);
    mel_gpu_cmd_buffer_barrier(cmd, dst, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_COMMON);
    mel_gpu_command_list_end(cmd);

    return mel_gpu__transfer_dispatch(dev, q, cmd, st.value);
}

Mel_Gpu_Future* mel_gpu_texture_upload_async(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Texture dst, Mel_Gpu_Texture_Region region, const void* data, usize size)
{
    if (!dev || !q || !data || size == 0)
    {
        mel_log_error("gpu", "texture_upload_async: invalid arguments (dev/queue/data/size)");
        return mel_gpu__transfer_fail(dev, MEL_GPU_TRANSFER_BAD_PARAMS);
    }
    Mel_Gpu_Texture_Obj o;
    if (!mel_gpu__texture_get(dev, dst, &o))
    {
        mel_log_error("gpu", "texture_upload_async: invalid destination texture handle");
        return mel_gpu__transfer_fail(dev, MEL_GPU_TRANSFER_BAD_PARAMS);
    }

    Mel_Gpu_Buffer_Create_Result st = mel_gpu_buffer_create(dev, .size = size, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_UPLOAD, .data = data, .name = "transfer-tex-staging");
    if (mel_gpu_failed(st.status))
    {
        mel_log_error("gpu", "texture_upload_async: staging buffer create failed");
        return mel_gpu__transfer_fail(dev, MEL_GPU_TRANSFER_BACKEND_FAILED);
    }
    VkBuffer staging_vk = VK_NULL_HANDLE;
    mel_gpu__buffer_get(dev, st.value, &staging_vk);

    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    if (!cmd)
    {
        mel_log_error("gpu", "texture_upload_async: command list create failed");
        mel_gpu_buffer_destroy(dev, st.value);
        return mel_gpu__transfer_fail(dev, MEL_GPU_TRANSFER_BACKEND_FAILED);
    }

    VkImageAspectFlags aspect = mel_gpu__aspect_flags(region.subresource.aspect, o.format);
    u32                layer_count = region.subresource.layer_count ? region.subresource.layer_count : 1;
    VkExtent3D         extent = { region.extent.width ? region.extent.width : o.width, region.extent.height ? region.extent.height : o.height, region.extent.depth ? region.extent.depth : 1 };

    mel_gpu_command_list_begin(cmd);

    VkImageMemoryBarrier to_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = o.image,
        .subresourceRange = { aspect, region.subresource.base_mip, 1, region.subresource.base_layer, layer_count },
    };
    vkCmdPipelineBarrier(cmd->cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_dst);

    VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { aspect, region.subresource.base_mip, region.subresource.base_layer, layer_count },
        .imageOffset = { (i32)region.offset.width, (i32)region.offset.height, (i32)region.offset.depth },
        .imageExtent = extent,
    };
    vkCmdCopyBufferToImage(cmd->cb, staging_vk, o.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier to_read = to_dst;
    to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &to_read);

    mel_gpu_command_list_end(cmd);

    return mel_gpu__transfer_dispatch(dev, q, cmd, st.value);
}
