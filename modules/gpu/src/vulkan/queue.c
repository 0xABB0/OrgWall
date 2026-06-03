#include "vk_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

static bool mel_gpu__role_reachable(Mel_Gpu_Queue_Role role)
{
    switch (role)
    {
    case MEL_GPU_QUEUE_GRAPHICS:
    case MEL_GPU_QUEUE_COMPUTE:
    case MEL_GPU_QUEUE_TRANSFER:
    case MEL_GPU_QUEUE_ASYNC_COMPUTE:
    case MEL_GPU_QUEUE_ASSET_IO:
        return true;
    default:
        return false;
    }
}

u32 mel_gpu_queue_available(Mel_Gpu_Device* dev, Mel_Gpu_Queue_Role role, Mel_Gpu_Queue_Priority priority)
{
    (void)dev;
    (void)priority;
    return mel_gpu__role_reachable(role) ? 1u : 0u;
}

Mel_Gpu_Queue* mel_gpu_queue_request_opt(Mel_Gpu_Device* dev, Mel_Gpu_Queue_Role role, Mel_Gpu_Queue_Request_Opt opt)
{
    if (!dev || !mel_gpu__role_reachable(role))
    {
        mel_log_error("gpu", "queue_request: role %d unavailable on this device", (int)role);
        return NULL;
    }

    if (opt.dedicated)
    {
        mel_log_error("gpu", "queue_request: dedicated family for role %d unavailable (M1 single-queue backend wires only the graphics family); refusing to promote upward", (int)role);
        return NULL;
    }

    if (role != MEL_GPU_QUEUE_GRAPHICS)
        mel_log_warn("gpu", "queue_request: role %d lowered to the graphics queue (M1 single-queue backend)", (int)role);

    if (opt.internally_synchronized && dev->caps.queues.internally_synchronized_queues == MEL_GPU_INTERNAL_SYNC_NONE)
    {
        if (!opt.allow_locked_fallback)
        {
            mel_log_error("gpu", "queue_request: internally_synchronized unavailable and no locked fallback allowed");
            return NULL;
        }
        mel_log_warn("gpu", "queue_request: internally_synchronized unavailable; returning a locked queue");
    }

    Mel_Gpu_Queue* q = mel_alloc_type(dev->alloc, Mel_Gpu_Queue);
    *q = (Mel_Gpu_Queue){ 0 };
    q->dev = dev;
    q->vk = dev->graphics_queue;
    q->family = dev->graphics_family;
    q->role = role;
    q->internally_synchronized = false;
    q->locked_fallback = opt.internally_synchronized;
    return q;
}

void mel_gpu_queue_release(Mel_Gpu_Queue* q)
{
    if (!q)
        return;
    mel_dealloc(q->dev->alloc, q);
}

Mel_Gpu_Queue_Info mel_gpu_queue_info(Mel_Gpu_Queue* q)
{
    Mel_Gpu_Queue_Info info = { 0 };
    if (!q)
        return info;

    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(q->dev->phys, &count, NULL);
    VkQueueFamilyProperties* fams = mel_alloc_array(mel_alloc_heap(), VkQueueFamilyProperties, count);
    vkGetPhysicalDeviceQueueFamilyProperties(q->dev->phys, &count, fams);

    if (q->family < count)
    {
        VkQueueFlags f = fams[q->family].queueFlags;
        info.family_index = q->family;
        info.supports_graphics = (f & VK_QUEUE_GRAPHICS_BIT) != 0;
        info.supports_compute = (f & VK_QUEUE_COMPUTE_BIT) != 0;
        info.supports_transfer = (f & VK_QUEUE_TRANSFER_BIT) != 0;
        info.supports_sparse_binding = (f & VK_QUEUE_SPARSE_BINDING_BIT) != 0;
        info.timestamp_valid_bits = fams[q->family].timestampValidBits;
    }
    mel_dealloc(mel_alloc_heap(), fams);
    return info;
}

static bool mel_gpu__submit_poller(void* user)
{
    Mel_Gpu_Device* dev = user;
    mel_mutex_lock(&dev->submit_lock);
    for (u32 i = 0; i < dev->pending_count;)
    {
        Mel_Gpu_Pending_Submit* p = &dev->pending[i];
        VkResult                s = vkGetFenceStatus(dev->vk, p->fence);
        if (s == VK_SUCCESS)
        {
            Mel_Gpu_Future* f = p->future;
            u64             serial = p->serial;
            vkDestroyFence(dev->vk, p->fence, NULL);
            dev->pending[i] = dev->pending[--dev->pending_count];
            mel_mutex_unlock(&dev->submit_lock);
            mel_gpu__submit_complete(dev, serial);
            mel_gpu_future_resolve(f, NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
            mel_mutex_lock(&dev->submit_lock);
        }
        else
        {
            i++;
        }
    }
    mel_mutex_unlock(&dev->submit_lock);
    return true;
}

static void mel_gpu__pending_push(Mel_Gpu_Device* dev, VkFence fence, Mel_Gpu_Future* future, u64 serial)
{
    mel_mutex_lock(&dev->submit_lock);
    if (dev->pending_count == dev->pending_cap)
    {
        u32 cap = dev->pending_cap ? dev->pending_cap * 2 : 16;
        dev->pending = dev->pending ? mel_realloc(dev->alloc, dev->pending, sizeof(Mel_Gpu_Pending_Submit) * cap) : mel_alloc(dev->alloc, sizeof(Mel_Gpu_Pending_Submit) * cap);
        dev->pending_cap = cap;
    }
    dev->pending[dev->pending_count++] = (Mel_Gpu_Pending_Submit){ .fence = fence, .future = future, .serial = serial, .active = true };
    bool need_poller = !dev->submit_poller_registered;
    dev->submit_poller_registered = true;
    mel_mutex_unlock(&dev->submit_lock);

    if (need_poller && dev->pump)
        mel_gpu_pump_add_poller(dev->pump, mel_gpu__submit_poller, dev);
}

Mel_Gpu_Future* mel_gpu_queue_submit(Mel_Gpu_Queue* q, Mel_Gpu_Submit submit)
{
    mel_assert(q && "queue_submit: null queue");
    Mel_Gpu_Device* dev = q->dev;

    Mel_Gpu_Concurrency submit_class = q->internally_synchronized ? MEL_GPU_CONCURRENCY_CONCURRENT : MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT;
    mel_gpu__track_enter(dev, q, submit_class);

    if (submit.command_list_count && !submit.command_lists)
    {
        mel_log_error("gpu", "queue_submit: command_list_count=%u but command_lists is NULL", submit.command_list_count);
        mel_assert(!"queue_submit: command_lists is NULL with a positive count");
        submit.command_list_count = 0;
    }

    u64 serial = mel_gpu__submit_serial_next(dev);

    VkCommandBuffer* cbs = submit.command_list_count ? mel_alloc_array(dev->alloc, VkCommandBuffer, submit.command_list_count) : NULL;
    for (u32 i = 0; i < submit.command_list_count; i++)
    {
        if (!submit.command_lists[i])
        {
            mel_log_error("gpu", "queue_submit: command_lists[%u] is NULL", i);
            mel_assert(!"queue_submit: null command list in batch");
            cbs[i] = VK_NULL_HANDLE;
            continue;
        }
        cbs[i] = submit.command_lists[i]->cb;
    }

    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence           fence = VK_NULL_HANDLE;
    vkCreateFence(dev->vk, &fci, NULL, &fence);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = submit.command_list_count,
        .pCommandBuffers = submit.command_list_count ? cbs : NULL,
    };

    mel_mutex_lock(&dev->submit_lock);
    VkResult r = vkQueueSubmit(q->vk, 1, &si, fence);
    mel_mutex_unlock(&dev->submit_lock);
    mel_gpu__track_exit(dev, q);

    if (cbs)
        mel_dealloc(dev->alloc, cbs);

    Mel_Gpu_Future* f = mel_gpu_future_create(dev->pump, dev->reactor);

    if (r != VK_SUCCESS)
    {
        mel_gpu__device_is_lost(dev, r, "queue_submit");
        vkDestroyFence(dev->vk, fence, NULL);
        mel_gpu__submit_complete(dev, serial);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR));
        return f;
    }

    if (dev->pump)
    {
        mel_gpu__pending_push(dev, fence, f, serial);
    }
    else
    {
        vkWaitForFences(dev->vk, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(dev->vk, fence, NULL);
        mel_gpu__submit_complete(dev, serial);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
    }
    return f;
}
