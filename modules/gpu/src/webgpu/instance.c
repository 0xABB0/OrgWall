#include "wgpu_backend.h"

#include <allocator/heap.h>
#include <log/log.h>
#include <thread/thread.h>

typedef struct
{
    WGPUAdapter adapter;
    bool        done;
    bool        ok;
} Mel_Gpu_Adapter_Request;

static void mel_gpu__adapter_cb(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* u1, void* u2)
{
    (void)u2;
    Mel_Gpu_Adapter_Request* req = (Mel_Gpu_Adapter_Request*)u1;
    req->done = true;
    if (status == WGPURequestAdapterStatus_Success && adapter)
    {
        req->adapter = adapter;
        req->ok = true;
    }
    else
    {
        mel_log_error("gpu", "requestAdapter failed (status %d): %.*s", (int)status, message.data ? (int)message.length : 0, message.data ? message.data : "");
    }
}

Mel_Gpu_Instance* mel_gpu_instance_create_opt(Mel_Gpu_Instance_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    WGPUInstance wgpu = wgpuCreateInstance(NULL);
    if (!wgpu)
    {
        mel_log_error("gpu", "wgpuCreateInstance returned null; no WebGPU instance available");
        return NULL;
    }

    Mel_Gpu_Instance* inst = mel_alloc_type(alloc, Mel_Gpu_Instance);
    *inst = (Mel_Gpu_Instance){ 0 };
    inst->debug = opt.debug;
    inst->alloc = alloc;
    inst->wgpu = wgpu;

    Mel_Gpu_Adapter_Request req = { 0 };
    WGPURequestAdapterOptions ropt = { .powerPreference = WGPUPowerPreference_HighPerformance };
    WGPURequestAdapterCallbackInfo cbi = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = mel_gpu__adapter_cb,
        .userdata1 = &req,
    };
    wgpuInstanceRequestAdapter(wgpu, &ropt, cbi);

    u32 spins = 0;
    while (!req.done && spins < 100000)
    {
        wgpuInstanceProcessEvents(wgpu);
        if (!req.done)
        {
            mel_thread_sleep(100000);
            spins++;
        }
    }

    if (!req.ok)
    {
        mel_log_error("gpu", "no WebGPU adapter granted; instance has 0 adapters");
        inst->adapters = NULL;
        inst->adapter_count = 0;
        return inst;
    }

    inst->adapters = mel_alloc_array(alloc, Mel_Gpu_Adapter, 1);
    inst->adapter_count = 1;
    inst->adapters[0].instance = inst;
    inst->adapters[0].wgpu = req.adapter;
    mel_gpu__caps_probe(req.adapter, &inst->adapters[0].caps);
    inst->adapters[0].caps.debug.validation_available = opt.debug.enabled;

    mel_log_info("gpu", "webgpu instance created: 1 adapter ('%s')", inst->adapters[0].caps.adapter.name);
    return inst;
}

void mel_gpu_instance_destroy(Mel_Gpu_Instance* inst)
{
    if (!inst)
        return;
    if (inst->adapters)
    {
        for (u32 i = 0; i < inst->adapter_count; i++)
            if (inst->adapters[i].wgpu)
                wgpuAdapterRelease(inst->adapters[i].wgpu);
        mel_dealloc(inst->alloc, inst->adapters);
    }
    if (inst->wgpu)
        wgpuInstanceRelease(inst->wgpu);
    mel_dealloc(inst->alloc, inst);
}

u32 mel_gpu_adapters(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter** out, u32 max)
{
    if (!inst)
        return 0;
    u32 n = inst->adapter_count < max ? inst->adapter_count : max;
    for (u32 i = 0; i < n; i++)
        out[i] = &inst->adapters[i];
    return inst->adapter_count;
}

Mel_Gpu_Caps mel_gpu_adapter_caps(Mel_Gpu_Adapter* adapter) { return adapter ? adapter->caps : (Mel_Gpu_Caps){ 0 }; }
