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
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    return o ? o->buf : VK_NULL_HANDLE;
}

VkSemaphore mel_gpu_vk_semaphore(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync)
{
    Mel_Gpu_Sync_Obj* o = mel_gpu__table_get(dev, &dev->syncs, sync.slot);
    return o ? o->semaphore : VK_NULL_HANDLE;
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
