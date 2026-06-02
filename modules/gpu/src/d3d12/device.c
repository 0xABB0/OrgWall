#include "d3d_backend.h"

#include <allocator/heap.h>
#include <log/log.h>
#include <thermal/thermal.h>
#include <power/power.h>

Mel_Gpu_Device_Create_Result mel_gpu_device_create_opt(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter* adapter, Mel_Gpu_Device_Opt opt)
{
    Mel_Gpu_Device_Create_Result res = { .value = NULL, .status = MEL_GPU_DEVICE_CREATE_OK };

    if (!inst || !adapter)
    {
        res.status = MEL_GPU_DEVICE_CREATE_NO_ADAPTER;
        mel_log_error("gpu", "device_create: null instance or adapter");
        return res;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    // Feature Level 12_0 is the support floor (gpu-rhi.md §2). The debug layer was already armed at
    // instance-create; the request-and-grant feature set (U4) is consumed per-unit in later phases.
    ID3D12Device* d3d = NULL;
    HRESULT       hr = D3D12CreateDevice((IUnknown*)adapter->dxgi, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void**)&d3d);
    if (FAILED(hr) || !d3d)
    {
        res.status = hr == E_OUTOFMEMORY ? MEL_GPU_DEVICE_CREATE_OOM : MEL_GPU_DEVICE_CREATE_VK_FAILED;
        mel_log_error("gpu", "D3D12CreateDevice failed: 0x%08lx", (unsigned long)hr);
        return res;
    }

    // U7: the DIRECT queue is the graphics/compute/copy-capable queue. Additional roles map to it on the
    // single-queue floor (gpu-rhi.md §7.1); async-compute / dedicated-transfer queues land later.
    D3D12_COMMAND_QUEUE_DESC qd = { .Type = D3D12_COMMAND_LIST_TYPE_DIRECT, .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE };
    ID3D12CommandQueue*      queue = NULL;
    hr = ID3D12Device_CreateCommandQueue(d3d, &qd, &IID_ID3D12CommandQueue, (void**)&queue);
    if (FAILED(hr) || !queue)
    {
        res.status = MEL_GPU_DEVICE_CREATE_NO_GRAPHICS_QUEUE;
        mel_log_error("gpu", "CreateCommandQueue(DIRECT) failed: 0x%08lx", (unsigned long)hr);
        ID3D12Device_Release(d3d);
        return res;
    }

    // U7/U3: the device timeline fence + its wait event (gpu-rhi.md §3.3).
    ID3D12Fence* fence = NULL;
    hr = ID3D12Device_CreateFence(d3d, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
    HANDLE event = hr == S_OK ? CreateEventW(NULL, FALSE, FALSE, NULL) : NULL;
    if (FAILED(hr) || !fence || !event)
    {
        res.status = MEL_GPU_DEVICE_CREATE_VK_FAILED;
        mel_log_error("gpu", "CreateFence/CreateEvent failed: 0x%08lx", (unsigned long)hr);
        if (event)
            CloseHandle(event);
        if (fence)
            ID3D12Fence_Release(fence);
        ID3D12CommandQueue_Release(queue);
        ID3D12Device_Release(d3d);
        return res;
    }

    Mel_Gpu_Device* dev = mel_alloc_type(alloc, Mel_Gpu_Device);
    *dev = (Mel_Gpu_Device){ 0 };
    dev->instance = inst;
    dev->adapter = adapter;
    dev->d3d = d3d;
    dev->direct_queue = queue;
    dev->timeline = fence;
    dev->fence_event = event;
    dev->caps = adapter->caps;
    dev->alloc = alloc;
    dev->reactor = opt.reactor;
    dev->debug = opt.debug;
    dev->on_device_lost = opt.on_device_lost;
    dev->device_lost_user = opt.device_lost_user;

    mel_gpu__caps_refine_device(d3d, queue, &dev->caps);

    dev->caps.power.power_source = (Mel_Gpu_Power_Source)mel_power_source_current();
    Mel_Thermal_Pressure tp = mel_thermal_current();
    dev->caps.power.thermal_pressure = tp > MEL_THERMAL_UNKNOWN ? (Mel_Gpu_Thermal_Tier)(tp - 1) : MEL_GPU_THERMAL_NOMINAL;
    dev->caps.power.low_power_mode = mel_power_low_power_current() == MEL_POWER_LOW_POWER_ON;

    mel_mutex_init(&dev->obj_lock, MEL_MUTEX_PLAIN);
    mel_mutex_init(&dev->submit_lock, MEL_MUTEX_PLAIN);
    mel_slotmap_init(&dev->buffers.map, alloc, .item_size = sizeof(Mel_Gpu_Buffer_Obj), .initial_capacity = 16);
    dev->buffers.init = true;

    if (opt.debug.thread_safety_tracker)
        dev->tracker = mel_gpu_thread_tracker_create();

    if (opt.reactor)
        dev->pump = mel_gpu_pump_create(opt.reactor);

    res.value = dev;
    mel_log_info("gpu", "device created on '%s'", dev->caps.adapter.name);
    return res;
}

static void mel_gpu__table_report_leaks(Mel_Gpu_Resource_Table* t, const char* kind)
{
    if (!t->init)
        return;
    u32 n = mel_slotmap_count(&t->map);
    if (n == 0)
        return;
    mel_log_error("gpu", "leak: %u live %s resource(s) at device destroy", n, kind);
    u8*   data = mel_slotmap_data(&t->map);
    usize stride = t->map.item_size;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Resource_Header* h = (Mel_Gpu_Resource_Header*)(data + (usize)i * stride);
        mel_log_error("gpu", "  leaked %s '%s'", kind, h->name ? h->name : "(unnamed)");
    }
}

void mel_gpu_device_destroy(Mel_Gpu_Device* dev)
{
    if (!dev)
        return;

    // Drain the GPU: signal a fresh serial and wait for it, so every submission has retired before teardown.
    if (dev->direct_queue && dev->timeline && !dev->lost)
    {
        u64 s = mel_gpu__submit_serial_next(dev);
        if (SUCCEEDED(ID3D12CommandQueue_Signal(dev->direct_queue, dev->timeline, s)))
            mel_gpu__wait_serial(dev, s);
        mel_gpu__submit_complete(dev, s);
    }
    else
    {
        mel_gpu__submit_complete(dev, dev->submit_serial);
    }
    if (dev->deferred)
        mel_dealloc(dev->alloc, dev->deferred);
    if (dev->pending)
        mel_dealloc(dev->alloc, dev->pending);

    mel_gpu__table_report_leaks(&dev->buffers, "buffer");
    if (dev->buffers.init)
        mel_slotmap_free(&dev->buffers.map);

    mel_mutex_destroy(&dev->obj_lock);
    mel_mutex_destroy(&dev->submit_lock);

    if (dev->pump)
        mel_gpu_pump_destroy(dev->pump);
    if (dev->tracker)
        mel_gpu_thread_tracker_destroy(dev->tracker);

    if (dev->fence_event)
        CloseHandle(dev->fence_event);
    if (dev->timeline)
        ID3D12Fence_Release(dev->timeline);
    if (dev->direct_queue)
        ID3D12CommandQueue_Release(dev->direct_queue);
    if (dev->d3d)
        ID3D12Device_Release(dev->d3d);

    Mel_Gpu_Instance* owned = dev->owns_instance ? dev->instance : NULL;
    const Mel_Alloc*  alloc = dev->alloc;
    mel_dealloc(alloc, dev);
    if (owned)
        mel_gpu_instance_destroy(owned);
}

const Mel_Gpu_Caps* mel_gpu_device_caps(Mel_Gpu_Device* dev) { return dev ? &dev->caps : NULL; }

Mel_Reactor* mel_gpu_device_reactor(Mel_Gpu_Device* dev) { return dev ? dev->reactor : NULL; }

// ---- U1 slotmap table helpers ----

Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj)
{
    mel_mutex_lock(&dev->obj_lock);
    Mel_SlotMap_Handle h = mel_slotmap_insert(&t->map, obj);
    mel_mutex_unlock(&dev->obj_lock);
    return h;
}

void* mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    void* p = mel_slotmap_get(&t->map, h);
    mel_mutex_unlock(&dev->obj_lock);
    return p;
}

bool mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    bool ok = mel_slotmap_remove(&t->map, h);
    mel_mutex_unlock(&dev->obj_lock);
    return ok;
}

bool mel_gpu__table_remove_deferred(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    bool ok = mel_slotmap_remove_deferred(&t->map, h);
    mel_mutex_unlock(&dev->obj_lock);
    return ok;
}

void mel_gpu__table_reclaim(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, u32 index)
{
    mel_mutex_lock(&dev->obj_lock);
    mel_slotmap_reclaim(&t->map, index);
    mel_mutex_unlock(&dev->obj_lock);
}

// ---- U3 future-gated retirement ----

static void mel_gpu__free_deferred_entry(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free* e)
{
    if (e->resource)
        ID3D12Resource_Release(e->resource);
    if (e->pso)
        ID3D12PipelineState_Release(e->pso);
    if (e->root_sig)
        ID3D12RootSignature_Release(e->root_sig);
    if (e->has_reclaim)
        mel_gpu__table_reclaim(dev, e->reclaim_table, e->reclaim_index);
}

void mel_gpu__wait_serial(Mel_Gpu_Device* dev, u64 serial)
{
    if (ID3D12Fence_GetCompletedValue(dev->timeline) >= serial)
        return;
    ID3D12Fence_SetEventOnCompletion(dev->timeline, serial, dev->fence_event);
    WaitForSingleObject(dev->fence_event, INFINITE);
}

u64 mel_gpu__submit_serial_next(Mel_Gpu_Device* dev)
{
    mel_mutex_lock(&dev->submit_lock);
    u64 s = ++dev->submit_serial;
    mel_mutex_unlock(&dev->submit_lock);
    return s;
}

void mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial)
{
    mel_mutex_lock(&dev->submit_lock);
    if (serial > dev->submit_completed)
        dev->submit_completed = serial;
    u64 wm = dev->submit_completed;
    u32 keep = 0;
    for (u32 i = 0; i < dev->deferred_count; i++)
    {
        if (dev->deferred[i].marker <= wm)
            mel_gpu__free_deferred_entry(dev, &dev->deferred[i]);
        else
            dev->deferred[keep++] = dev->deferred[i];
    }
    dev->deferred_count = keep;
    mel_mutex_unlock(&dev->submit_lock);
}

void mel_gpu__defer_free(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free entry)
{
    mel_mutex_lock(&dev->submit_lock);
    entry.marker = dev->submit_serial;
    if (entry.marker <= dev->submit_completed)
    {
        mel_mutex_unlock(&dev->submit_lock);
        mel_gpu__free_deferred_entry(dev, &entry);
        return;
    }
    if (dev->deferred_count == dev->deferred_cap)
    {
        u32 cap = dev->deferred_cap ? dev->deferred_cap * 2 : 16;
        dev->deferred = dev->deferred ? mel_realloc(dev->alloc, dev->deferred, sizeof(Mel_Gpu_Deferred_Free) * cap) : mel_alloc(dev->alloc, sizeof(Mel_Gpu_Deferred_Free) * cap);
        dev->deferred_cap = cap;
    }
    dev->deferred[dev->deferred_count++] = entry;
    mel_mutex_unlock(&dev->submit_lock);
}

// ---- device_create_default ----

static Mel_Gpu_Adapter* mel_gpu__pick_adapter(Mel_Gpu_Instance* inst, Mel_Gpu_Power_Preference pref)
{
    Mel_Gpu_Adapter* adapters[16];
    u32              n = mel_gpu_adapters(inst, adapters, 16);
    if (n == 0)
        return NULL;
    if (n > 16)
        n = 16;

    Mel_Gpu_Adapter_Type want = pref == MEL_GPU_POWER_PREFERENCE_LOW ? MEL_GPU_ADAPTER_INTEGRATED : MEL_GPU_ADAPTER_DISCRETE;
    for (u32 i = 0; i < n; i++)
        if (adapters[i]->caps.adapter.adapter_type == want)
            return adapters[i];
    return adapters[0];
}

Mel_Gpu_Future* mel_gpu_device_create_default_opt(Mel_Gpu_Device_Default_Opt opt)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = opt.app_name, .debug = opt.debug);
    Mel_Gpu_Adapter*  adapter = inst ? mel_gpu__pick_adapter(inst, opt.power_preference) : NULL;

    Mel_Gpu_Device_Create_Result dr = { 0 };
    if (adapter)
        dr = mel_gpu_device_create(inst, adapter, .reactor = opt.reactor, .features = opt.features, .debug = opt.debug, .power_preference = opt.power_preference);

    Mel_Gpu_Completion_Pump* pump = dr.value ? dr.value->pump : NULL;
    Mel_Gpu_Future*          f = mel_gpu_future_create(pump, opt.reactor);

    if (dr.value)
    {
        dr.value->owns_instance = true;
        mel_gpu_future_resolve(f, dr.value, dr.status);
    }
    else
    {
        if (inst)
            mel_gpu_instance_destroy(inst);
        mel_gpu_future_resolve(f, NULL, dr.status ? dr.status : MEL_GPU_DEVICE_CREATE_NO_ADAPTER);
    }
    return f;
}
