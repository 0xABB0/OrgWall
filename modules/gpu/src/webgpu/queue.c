#include "wgpu_backend.h"

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
        mel_log_warn("gpu", "queue_request: role %d lowered to the device queue (WebGPU single implicit queue)", (int)role);

    if (opt.internally_synchronized)
    {
        if (!opt.allow_locked_fallback)
        {
            mel_log_error("gpu", "queue_request: internally_synchronized unavailable on the WebGPU backend and no locked fallback allowed");
            return NULL;
        }
        mel_log_warn("gpu", "queue_request: internally_synchronized unavailable on the WebGPU backend; returning a serialized queue");
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

typedef struct
{
    bool done;
    bool ok;
} Mel_Gpu_Work_Done;

#ifdef __EMSCRIPTEN__
static void mel_gpu__work_done_cb(WGPUQueueWorkDoneStatus status, WGPUStringView message, void* u1, void* u2)
{
    (void)message;
#else
static void mel_gpu__work_done_cb(WGPUQueueWorkDoneStatus status, void* u1, void* u2)
{
#endif
    (void)u2;
    Mel_Gpu_Work_Done* w = (Mel_Gpu_Work_Done*)u1;
    w->done = true;
    w->ok = status == WGPUQueueWorkDoneStatus_Success;
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

    if (submit.wait_count || submit.signal_count)
        mel_log_warn("gpu", "queue_submit: explicit wait/signal sync primitives are ignored on the WebGPU backend (single implicit queue; use the completion future)");

    u64             serial = mel_gpu__submit_serial_next(dev);
    Mel_Gpu_Future* f = mel_gpu_future_create(dev->pump, dev->reactor);

    u32                buffer_count = 0;
    WGPUCommandBuffer  stack[8];
    WGPUCommandBuffer* buffers = stack;
    if (submit.command_list_count > 8)
        buffers = mel_alloc_array(dev->alloc, WGPUCommandBuffer, submit.command_list_count);

    for (u32 i = 0; i < submit.command_list_count; i++)
    {
        Mel_Gpu_Command_List* cmd = submit.command_lists[i];
        if (!cmd || !cmd->encoder)
        {
            mel_log_error("gpu", "queue_submit: command_lists[%u] is null or has no encoder", i);
            continue;
        }
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(cmd->encoder, NULL);
        wgpuCommandEncoderRelease(cmd->encoder);
        cmd->encoder = NULL;
        buffers[buffer_count++] = cb;
    }

    if (buffer_count)
        wgpuQueueSubmit(dev->queue, buffer_count, buffers);

    /* Drain to completion synchronously: the visual harness reads the future status
       immediately after submit, and headless runs have no reactor pumping the instance.
       This is the off-reactor *_sync pattern (spec §3.3). */
    Mel_Gpu_Work_Done       w = { 0 };
    WGPUQueueWorkDoneCallbackInfo cbi = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = mel_gpu__work_done_cb, .userdata1 = &w };
    wgpuQueueOnSubmittedWorkDone(dev->queue, cbi);

    mel_gpu__drain_until(dev->wgpu_instance, &w.done);

    for (u32 i = 0; i < buffer_count; i++)
        wgpuCommandBufferRelease(buffers[i]);
    if (buffers != stack)
        mel_dealloc(dev->alloc, buffers);

    mel_gpu__submit_complete(dev, serial);
    mel_gpu_future_resolve(f, NULL, w.ok || buffer_count == 0 ? MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK) : MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR));

    mel_gpu__track_exit(dev, q);
    return f;
}
