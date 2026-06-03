#include "mtl_backend.h"

#include <allocator/heap.h>
#include <log/log.h>
#include <power/power.h>
#include <thermal/thermal.h>

#include <string.h>

Mel_Gpu_Device_Create_Result mel_gpu_device_create_opt(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter* adapter, Mel_Gpu_Device_Opt opt)
{
    Mel_Gpu_Device_Create_Result res = { .value = NULL, .status = MEL_GPU_DEVICE_CREATE_OK };

    if (!inst || !adapter)
    {
        res.status = MEL_GPU_DEVICE_CREATE_NO_ADAPTER;
        mel_log_error("gpu", "device_create: null instance or adapter");
        return res;
    }

    id<MTLDevice>       mtl = adapter->mtl;
    id<MTLCommandQueue> queue = [mtl newCommandQueue];
    if (!queue)
    {
        res.status = MEL_GPU_DEVICE_CREATE_NO_GRAPHICS_QUEUE;
        mel_log_error("gpu", "device_create: newCommandQueue returned nil");
        return res;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Gpu_Device* dev = mel_alloc_type(alloc, Mel_Gpu_Device);
    *dev = (Mel_Gpu_Device){ 0 };
    dev->instance = inst;
    dev->adapter = adapter;
    dev->mtl = mtl;
    dev->queue = queue;
    dev->caps = adapter->caps;
    dev->alloc = alloc;
    dev->reactor = opt.reactor;
    dev->debug = opt.debug;
    dev->on_device_lost = opt.on_device_lost;
    dev->device_lost_user = opt.device_lost_user;

    mel_mutex_init(&dev->obj_lock, MEL_MUTEX_PLAIN);
    mel_mutex_init(&dev->submit_lock, MEL_MUTEX_PLAIN);

    mel_slotmap_init(&dev->buffers.map, alloc, .item_size = sizeof(Mel_Gpu_Buffer_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->textures.map, alloc, .item_size = sizeof(Mel_Gpu_Texture_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->texture_views.map, alloc, .item_size = sizeof(Mel_Gpu_Texture_View_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->samplers.map, alloc, .item_size = sizeof(Mel_Gpu_Sampler_Obj), .initial_capacity = 16);
    dev->buffers.init = dev->textures.init = dev->texture_views.init = dev->samplers.init = true;

    if (opt.features.descriptor_indexing)
        mel_log_warn("gpu", "device_create: bindless (descriptor_indexing) requested but not implemented on the Metal backend; caps.memory.bindless stays tier=none");
    if (opt.features.ray_tracing)
        mel_log_warn("gpu", "device_create: ray_tracing requested but not implemented on the Metal backend");
    if (opt.features.mesh_shaders)
        mel_log_warn("gpu", "device_create: mesh_shaders requested but not implemented on the Metal backend");

    if (opt.debug.thread_safety_tracker)
        dev->tracker = mel_gpu_thread_tracker_create();

    if (opt.reactor)
        dev->pump = mel_gpu_pump_create(opt.reactor);

    res.value = dev;
    mel_log_info("gpu", "metal device created on '%s'", dev->caps.adapter.name);
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
}

void mel_gpu_device_destroy(Mel_Gpu_Device* dev)
{
    if (!dev)
        return;

    mel_gpu__submit_complete(dev, dev->submit_serial);

    mel_gpu__table_report_leaks(&dev->buffers, "buffer");
    mel_gpu__table_report_leaks(&dev->textures, "texture");
    mel_gpu__table_report_leaks(&dev->texture_views, "texture-view");
    mel_gpu__table_report_leaks(&dev->samplers, "sampler");

    mel_slotmap_free(&dev->buffers.map);
    mel_slotmap_free(&dev->textures.map);
    mel_slotmap_free(&dev->texture_views.map);
    mel_slotmap_free(&dev->samplers.map);

    if (dev->pending)
        mel_dealloc(dev->alloc, dev->pending);

    mel_mutex_destroy(&dev->obj_lock);
    mel_mutex_destroy(&dev->submit_lock);

    if (dev->pump)
        mel_gpu_pump_destroy(dev->pump);
    if (dev->tracker)
        mel_gpu_thread_tracker_destroy(dev->tracker);

    dev->queue = nil;
    dev->mtl = nil;

    Mel_Gpu_Instance* owned = dev->owns_instance ? dev->instance : NULL;
    const Mel_Alloc*  alloc = dev->alloc;
    mel_dealloc(alloc, dev);
    if (owned)
        mel_gpu_instance_destroy(owned);
}

const Mel_Gpu_Caps* mel_gpu_device_caps(Mel_Gpu_Device* dev) { return dev ? &dev->caps : NULL; }

Mel_Reactor* mel_gpu_device_reactor(Mel_Gpu_Device* dev) { return dev ? dev->reactor : NULL; }

void mel_gpu__track_enter(Mel_Gpu_Device* dev, const void* object, Mel_Gpu_Concurrency cls)
{
    if (dev->tracker)
        mel_gpu_thread_tracker_enter(dev->tracker, object, cls);
}

void mel_gpu__track_exit(Mel_Gpu_Device* dev, const void* object)
{
    if (dev->tracker)
        mel_gpu_thread_tracker_exit(dev->tracker, object);
}

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

bool mel_gpu__table_get_copy(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h, void* out)
{
    mel_mutex_lock(&dev->obj_lock);
    void* p = mel_slotmap_get(&t->map, h);
    if (p)
        memcpy(out, p, t->map.item_size);
    mel_mutex_unlock(&dev->obj_lock);
    return p != NULL;
}

bool mel_gpu__table_alive(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    bool alive = mel_slotmap_get(&t->map, h) != NULL;
    mel_mutex_unlock(&dev->obj_lock);
    return alive;
}

bool mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    bool ok = mel_slotmap_remove(&t->map, h);
    mel_mutex_unlock(&dev->obj_lock);
    return ok;
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
    mel_mutex_unlock(&dev->submit_lock);
}

Mel_Gpu_Memory_Budget mel_gpu_memory_budget(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Memory_Budget b = { 0 };
    if (!dev)
        return b;
    b.budget_bytes = (u64)dev->mtl.recommendedMaxWorkingSetSize;
    b.usage_bytes = (u64)dev->mtl.currentAllocatedSize;
    return b;
}

void mel_gpu_set_budget_pressure_callback(Mel_Gpu_Device* dev, Mel_Gpu_Budget_Pressure_Fn cb, void* user)
{
    (void)cb;
    (void)user;
    if (dev)
        mel_log_warn("gpu", "set_budget_pressure_callback: not implemented on the Metal backend; no pressure events will fire");
}

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
