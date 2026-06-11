#include "mtl_backend.h"

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
        mel_log_warn("gpu", "queue_request: role %d lowered to the device command queue (M1 single-queue Metal backend)", (int)role);

    if (opt.internally_synchronized)
    {
        if (!opt.allow_locked_fallback)
        {
            mel_log_error("gpu", "queue_request: internally_synchronized unavailable on the Metal backend and no locked fallback allowed");
            return NULL;
        }
        mel_log_warn("gpu", "queue_request: internally_synchronized unavailable on the Metal backend; returning a serialized queue");
    }

    Mel_Gpu_Queue* q = mel_alloc_type(dev->alloc, Mel_Gpu_Queue);
    *q = (Mel_Gpu_Queue){ 0 };
    q->dev = dev;
    q->role = role;
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
    info.timestamp_valid_bits = 0;
    return info;
}

Mel_Gpu_Future* mel_gpu_queue_submit(Mel_Gpu_Queue* q, Mel_Gpu_Submit submit)
{
    mel_assert(q && "queue_submit: null queue");
    Mel_Gpu_Device* dev = q->dev;

    mel_gpu__track_enter(dev, q, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);

    if (submit.command_list_count && !submit.command_lists)
    {
        mel_log_error("gpu", "queue_submit: command_list_count=%u but command_lists is NULL", submit.command_list_count);
        mel_assert(!"queue_submit: command_lists is NULL with a positive count");
        submit.command_list_count = 0;
    }

    u64             serial = mel_gpu__submit_serial_next(dev);
    Mel_Gpu_Future* f = mel_gpu_future_create(dev->pump, dev->vat);
    mel_gpu__bindless_residency_flush(dev);

    for (u32 i = 0; i < submit.command_list_count; i++)
    {
        Mel_Gpu_Command_List* cmd = submit.command_lists[i];
        if (!cmd)
        {
            mel_log_error("gpu", "queue_submit: command_lists[%u] is NULL", i);
            mel_assert(!"queue_submit: null command list in batch");
            continue;
        }
        if (!cmd->cb)
        {
            mel_log_error("gpu", "queue_submit: command_lists[%u] has no recorded command buffer", i);
            continue;
        }
        bool last = (i + 1 == submit.command_list_count);
        if (last)
        {
            __block Mel_Gpu_Device* bdev = dev;
            __block Mel_Gpu_Future* bf = f;
            __block u64             bserial = serial;
            [cmd->cb addCompletedHandler:^(id<MTLCommandBuffer> buf) {
                (void)buf;
                mel_gpu__submit_complete(bdev, bserial);
                mel_gpu_future_resolve(bf, NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
            }];
        }
        [cmd->cb commit];
        cmd->cb = nil;
    }

    mel_gpu__track_exit(dev, q);

    if (submit.command_list_count == 0)
    {
        mel_gpu__submit_complete(dev, serial);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
    }
    return f;
}
