#include "d3d_backend.h"

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

    if (role != MEL_GPU_QUEUE_GRAPHICS)
        mel_log_warn("gpu", "queue_request: role %d lowered to the DIRECT queue (single-queue backend)", (int)role);

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
    q->d3d = dev->direct_queue;
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
    info.family_index = 0;
    info.supports_graphics = true;
    info.supports_compute = true;
    info.supports_transfer = true;
    info.supports_sparse_binding = false;
    info.timestamp_valid_bits = 64;
    return info;
}

static bool mel_gpu__submit_poller(void* user)
{
    Mel_Gpu_Device* dev = user;
    u64             completed = ID3D12Fence_GetCompletedValue(dev->timeline);
    mel_mutex_lock(&dev->submit_lock);
    for (u32 i = 0; i < dev->pending_count;)
    {
        if (dev->pending[i].serial <= completed)
        {
            Mel_Gpu_Future* f = dev->pending[i].future;
            u64             serial = dev->pending[i].serial;
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

static void mel_gpu__pending_push(Mel_Gpu_Device* dev, Mel_Gpu_Future* future, u64 serial)
{
    mel_mutex_lock(&dev->submit_lock);
    if (dev->pending_count == dev->pending_cap)
    {
        u32 cap = dev->pending_cap ? dev->pending_cap * 2 : 16;
        dev->pending = dev->pending ? mel_realloc(dev->alloc, dev->pending, sizeof(Mel_Gpu_Pending_Submit) * cap) : mel_alloc(dev->alloc, sizeof(Mel_Gpu_Pending_Submit) * cap);
        dev->pending_cap = cap;
    }
    dev->pending[dev->pending_count++] = (Mel_Gpu_Pending_Submit){ .future = future, .serial = serial };
    bool need_poller = !dev->submit_poller_registered;
    dev->submit_poller_registered = true;
    mel_mutex_unlock(&dev->submit_lock);

    if (need_poller && dev->pump)
        mel_gpu_pump_add_poller(dev->pump, mel_gpu__submit_poller, dev);
}

Mel_Gpu_Future* mel_gpu_queue_submit(Mel_Gpu_Queue* q, Mel_Gpu_Submit submit)
{
    Mel_Gpu_Device* dev = q->dev;
    u64             serial = mel_gpu__submit_serial_next(dev);

    if (submit.wait_count || submit.signal_count)
        mel_log_error("gpu", "queue_submit: wait/signal sync arrays are not yet wired on the D3D12 backend (Vulkan-only this milestone); %u wait + %u signal ignored", submit.wait_count, submit.signal_count);

    ID3D12CommandList*  stackbuf[8];
    ID3D12CommandList** cls = submit.command_list_count <= 8 ? stackbuf : mel_alloc_array(dev->alloc, ID3D12CommandList*, submit.command_list_count);
    for (u32 i = 0; i < submit.command_list_count; i++)
        cls[i] = (ID3D12CommandList*)submit.command_lists[i]->list;

    mel_mutex_lock(&dev->submit_lock);
    if (submit.command_list_count)
        ID3D12CommandQueue_ExecuteCommandLists(q->d3d, submit.command_list_count, cls);
    HRESULT hr = ID3D12CommandQueue_Signal(q->d3d, dev->timeline, serial);
    mel_mutex_unlock(&dev->submit_lock);

    if (cls != stackbuf)
        mel_dealloc(dev->alloc, cls);

    Mel_Gpu_Future* f = mel_gpu_future_create(dev->pump, dev->vat);

    if (FAILED(hr))
    {
        mel_log_error("gpu", "queue_submit: Signal failed: 0x%08lx", (unsigned long)hr);
        mel_gpu__submit_complete(dev, serial);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR));
        return f;
    }

    if (dev->pump)
    {
        mel_gpu__pending_push(dev, f, serial);
    }
    else
    {
        mel_gpu__wait_serial(dev, serial);
        mel_gpu__submit_complete(dev, serial);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
    }
    return f;
}
