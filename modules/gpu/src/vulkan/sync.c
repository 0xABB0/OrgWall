#include "vk_backend.h"

#include <gpu/sync.h>
#include <log/log.h>

Mel_Gpu_Sync_Create_Result mel_gpu_sync_create(Mel_Gpu_Device* dev, Mel_Gpu_Sync_Kind kind, u64 initial_value)
{
    Mel_Gpu_Sync_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SYNC_CREATE_OK };

    if (kind == MEL_GPU_SYNC_TIMELINE && dev->caps.queues.timeline != MEL_GPU_TIMELINE_NATIVE)
    {
        res.status = MEL_GPU_SYNC_CREATE_UNSUPPORTED;
        mel_log_error("gpu", "timeline semaphores unavailable on this device");
        return res;
    }

    VkSemaphoreTypeCreateInfo type = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = kind == MEL_GPU_SYNC_TIMELINE ? VK_SEMAPHORE_TYPE_TIMELINE : VK_SEMAPHORE_TYPE_BINARY,
        .initialValue = initial_value,
    };
    VkSemaphoreCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type };

    VkSemaphore sem = VK_NULL_HANDLE;
    VkResult    r = vkCreateSemaphore(dev->vk, &ci, NULL, &sem);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkCreateSemaphore failed: %s", mel_gpu__vk_result_str(r));
        res.status = MEL_GPU_SYNC_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Sync_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.semaphore = sem;
    obj.is_timeline = kind == MEL_GPU_SYNC_TIMELINE;
    res.value.slot = mel_gpu__table_insert(dev, &dev->syncs, &obj);
    return res;
}

void mel_gpu_sync_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync)
{
    const void* trk = mel_gpu__track_key(&dev->syncs, sync.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    Mel_Gpu_Sync_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->syncs, sync.slot, &o))
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    bool        borrowed = o.header.ownership == MEL_GPU_OWNERSHIP_BORROWED;
    VkSemaphore sem = o.semaphore;
    mel_gpu__table_remove(dev, &dev->syncs, sync.slot);
    if (!borrowed && sem)
        vkDestroySemaphore(dev->vk, sem, NULL);
    mel_gpu__track_exit(dev, trk);
}

bool mel_gpu_sync_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync) { return mel_gpu__table_alive(dev, &dev->syncs, sync.slot); }
