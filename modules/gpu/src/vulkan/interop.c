#include "vk_backend.h"

#include <gpu/vulkan/interop.h>
#include <log/log.h>

VkInstance mel_gpu_vk_instance(Mel_Gpu_Instance* inst) { return inst ? inst->vk : VK_NULL_HANDLE; }

VkPhysicalDevice mel_gpu_vk_physical_device(Mel_Gpu_Device* dev) { return dev ? dev->phys : VK_NULL_HANDLE; }

VkDevice mel_gpu_vk_device(Mel_Gpu_Device* dev) { return dev ? dev->vk : VK_NULL_HANDLE; }

VkQueue mel_gpu_vk_queue(Mel_Gpu_Queue* q) { return q ? q->vk : VK_NULL_HANDLE; }

VkCommandBuffer mel_gpu_vk_command_buffer(Mel_Gpu_Command_List* cmd) { return cmd ? cmd->cb : VK_NULL_HANDLE; }

VkBuffer mel_gpu_vk_buffer(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj o; // BUG-1: snapshot under obj_lock
    return mel_gpu__table_get_copy(dev, &dev->buffers, buf.slot, &o) ? o.buf : VK_NULL_HANDLE;
}

VkSemaphore mel_gpu_vk_semaphore(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync)
{
    Mel_Gpu_Sync_Obj o; // BUG-1: snapshot under obj_lock
    return mel_gpu__table_get_copy(dev, &dev->syncs, sync.slot, &o) ? o.semaphore : VK_NULL_HANDLE;
}

Mel_Gpu_Sync mel_gpu_sync_import(Mel_Gpu_Device* dev, VkSemaphore native, bool timeline)
{
    Mel_Gpu_Sync_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_BORROWED;
    obj.semaphore = native;
    obj.is_timeline = timeline;
    Mel_Gpu_Sync h = { mel_gpu__table_insert(dev, &dev->syncs, &obj) };
    return h;
}

void mel_gpu_cmd_assume_state(Mel_Gpu_Command_List* cmd) { (void)cmd; }

void mel_gpu_vk_cmd_image_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, VkImageSubresourceRange range,
                                  VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkImageLayout old_layout,
                                  VkPipelineStageFlags dst_stage, VkAccessFlags dst_access, VkImageLayout new_layout)
{
    Mel_Gpu_Texture_Obj o; // BUG-1: snapshot the immutable texture record under obj_lock
    if (!cmd || !mel_gpu__texture_get(cmd->dev, tex, &o))
    {
        mel_assert(!"vk_cmd_image_barrier: invalid texture handle");
        return;
    }
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = o.image,
        .subresourceRange = range,
    };
    vkCmdPipelineBarrier(cmd->cb, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}
