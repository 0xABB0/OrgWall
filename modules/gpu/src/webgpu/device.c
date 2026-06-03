#include "wgpu_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    WGPUDevice device;
    bool       done;
    bool       ok;
} Mel_Gpu_Device_Request;

static void mel_gpu__device_cb(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* u1, void* u2)
{
    (void)u2;
    Mel_Gpu_Device_Request* req = (Mel_Gpu_Device_Request*)u1;
    req->done = true;
    if (status == WGPURequestDeviceStatus_Success && device)
    {
        req->device = device;
        req->ok = true;
    }
    else
    {
        mel_log_error("gpu", "requestDevice failed (status %d): %.*s", (int)status, message.data ? (int)message.length : 0, message.data ? message.data : "");
    }
}

static void mel_gpu__uncaptured_error_cb(WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* u1, void* u2)
{
    (void)device;
    (void)u1;
    (void)u2;
    mel_log_error("gpu", "webgpu uncaptured error (type %d): %.*s", (int)type, message.data ? (int)message.length : 0, message.data ? message.data : "");
}

static void mel_gpu__device_lost_cb(WGPUDevice const* device, WGPUDeviceLostReason reason, WGPUStringView message, void* u1, void* u2)
{
    (void)device;
    (void)u2;
    Mel_Gpu_Device* dev = (Mel_Gpu_Device*)u1;
    if (!dev)
        return;
    /* Destroyed / InstanceDropped / FailedCreation are orderly teardown, not a runtime
       loss; only Unknown is a genuine device-lost event to surface (MEL-ENGINE-VIII). */
    if (reason != WGPUDeviceLostReason_Unknown)
        return;
    dev->lost = true;
    mel_log_error("gpu", "webgpu device lost: %.*s", message.data ? (int)message.length : 0, message.data ? message.data : "");
    if (dev->on_device_lost)
        dev->on_device_lost(dev, message.data ? message.data : "device lost", dev->device_lost_user);
}

static void mel_gpu__warn_unsupported_feature(const char* name)
{
    mel_log_warn("gpu", "device_create: feature '%s' requested but not available on the WebGPU backend; caps report the honest tier", name);
}

Mel_Gpu_Device_Create_Result mel_gpu_device_create_opt(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter* adapter, Mel_Gpu_Device_Opt opt)
{
    Mel_Gpu_Device_Create_Result res = { .value = NULL, .status = MEL_GPU_DEVICE_CREATE_OK };

    if (!inst || !adapter || !adapter->wgpu)
    {
        res.status = MEL_GPU_DEVICE_CREATE_NO_ADAPTER;
        mel_log_error("gpu", "device_create: null instance or adapter");
        return res;
    }

    if (opt.features.ray_tracing)
        mel_gpu__warn_unsupported_feature("ray_tracing");
    if (opt.features.mesh_shaders)
        mel_gpu__warn_unsupported_feature("mesh_shaders");
    if (opt.features.descriptor_indexing)
        mel_gpu__warn_unsupported_feature("descriptor_indexing (bindless stays tier=capped)");
    if (opt.features.host_image_copy)
        mel_gpu__warn_unsupported_feature("host_image_copy");

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Gpu_Device* dev = mel_alloc_type(alloc, Mel_Gpu_Device);
    *dev = (Mel_Gpu_Device){ 0 };

    WGPUDeviceDescriptor desc = {
        .label = mel_gpu__sv(opt.debug.enabled ? "mel-webgpu-device" : NULL),
        .uncapturedErrorCallbackInfo = { .callback = mel_gpu__uncaptured_error_cb },
        .deviceLostCallbackInfo = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = mel_gpu__device_lost_cb, .userdata1 = dev },
    };

    Mel_Gpu_Device_Request req = { 0 };
    WGPURequestDeviceCallbackInfo cbi = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = mel_gpu__device_cb,
        .userdata1 = &req,
    };
    wgpuAdapterRequestDevice(adapter->wgpu, &desc, cbi);

    mel_gpu__drain_until(inst->wgpu, &req.done);

    if (!req.ok)
    {
        res.status = MEL_GPU_DEVICE_CREATE_VK_FAILED;
        mel_log_error("gpu", "device_create: requestDevice did not resolve to a device");
        mel_dealloc(alloc, dev);
        return res;
    }

    WGPUQueue queue = wgpuDeviceGetQueue(req.device);
    if (!queue)
    {
        res.status = MEL_GPU_DEVICE_CREATE_NO_GRAPHICS_QUEUE;
        mel_log_error("gpu", "device_create: wgpuDeviceGetQueue returned null");
        wgpuDeviceRelease(req.device);
        mel_dealloc(alloc, dev);
        return res;
    }

    dev->instance = inst;
    dev->adapter = adapter;
    dev->wgpu_instance = inst->wgpu;
    dev->wgpu = req.device;
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
    mel_slotmap_init(&dev->shaders.map, alloc, .item_size = sizeof(Mel_Gpu_Shader_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->pipelines.map, alloc, .item_size = sizeof(Mel_Gpu_Pipeline_Obj), .initial_capacity = 16);
    dev->buffers.init = dev->textures.init = dev->texture_views.init = dev->samplers.init = dev->shaders.init = dev->pipelines.init = true;

    if (opt.debug.thread_safety_tracker)
        dev->tracker = mel_gpu_thread_tracker_create();

    if (opt.reactor)
    {
        dev->pump = mel_gpu_pump_create(opt.reactor);
        /* WebGPU completion source (spec §3.3 "Pump on tick"): one ProcessEvents
           tick-source per instance, serviced from the device's reactor. */
        mel_gpu_pump_add_poller(dev->pump, mel_gpu__instance_pump_tick, dev);
    }

    res.value = dev;
    mel_log_info("gpu", "webgpu device created on '%s'", dev->caps.adapter.name);
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

    if (dev->pump)
        mel_gpu_pump_remove_poller(dev->pump, mel_gpu__instance_pump_tick, dev);

    mel_gpu__submit_complete(dev, dev->submit_serial);

    mel_gpu__table_report_leaks(&dev->buffers, "buffer");
    mel_gpu__table_report_leaks(&dev->textures, "texture");
    mel_gpu__table_report_leaks(&dev->texture_views, "texture-view");
    mel_gpu__table_report_leaks(&dev->samplers, "sampler");
    mel_gpu__table_report_leaks(&dev->shaders, "shader");
    mel_gpu__table_report_leaks(&dev->pipelines, "pipeline");

    mel_slotmap_free(&dev->buffers.map);
    mel_slotmap_free(&dev->textures.map);
    mel_slotmap_free(&dev->texture_views.map);
    mel_slotmap_free(&dev->samplers.map);
    mel_slotmap_free(&dev->shaders.map);
    mel_slotmap_free(&dev->pipelines.map);

    mel_mutex_destroy(&dev->obj_lock);
    mel_mutex_destroy(&dev->submit_lock);

    if (dev->pump)
        mel_gpu_pump_destroy(dev->pump);
    if (dev->tracker)
        mel_gpu_thread_tracker_destroy(dev->tracker);

    if (dev->queue)
        wgpuQueueRelease(dev->queue);
    if (dev->wgpu)
        wgpuDeviceRelease(dev->wgpu);

    Mel_Gpu_Instance* owned = dev->owns_instance ? dev->instance : NULL;
    const Mel_Alloc*  alloc = dev->alloc;
    mel_dealloc(alloc, dev);
    if (owned)
        mel_gpu_instance_destroy(owned);
}

const Mel_Gpu_Caps* mel_gpu_device_caps(Mel_Gpu_Device* dev) { return dev ? &dev->caps : NULL; }

Mel_Reactor* mel_gpu_device_reactor(Mel_Gpu_Device* dev) { return dev ? dev->reactor : NULL; }

Mel_Gpu_Memory_Budget mel_gpu_memory_budget(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Memory_Budget b = { 0 };
    if (!dev)
        return b;
    b.budget_bytes = dev->caps.memory.device_local_bytes;
    b.usage_bytes = 0;
    return b;
}

void mel_gpu_set_budget_pressure_callback(Mel_Gpu_Device* dev, Mel_Gpu_Budget_Pressure_Fn cb, void* user)
{
    (void)cb;
    (void)user;
    if (dev)
        mel_log_warn("gpu", "set_budget_pressure_callback: not implemented on the WebGPU backend; no pressure events will fire");
}

static Mel_Gpu_Adapter* mel_gpu__pick_adapter(Mel_Gpu_Instance* inst, Mel_Gpu_Power_Preference pref)
{
    (void)pref;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
        return NULL;
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
