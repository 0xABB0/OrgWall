#include "webgpu_backend.h"

#if defined(__EMSCRIPTEN__)

#include <emscripten/html5_webgpu.h>

Mel_Gpu_Device* mel_gpu_device_create_opt(Mel_Gpu_Device_Opt opt)
{
    (void)opt;
    WGPUDevice device = emscripten_webgpu_get_device();
    if (!device) return NULL;

    Mel_Gpu_Device* dev = calloc(1, sizeof *dev);
    if (!dev) return NULL;
    dev->instance = wgpuCreateInstance(NULL);
    dev->device   = device;
    dev->queue    = wgpuDeviceGetQueue(device);
    return dev;
}

#else

// Native WebGPU (Dawn) acquires the adapter and device asynchronously; the
// AllowProcessEvents callback mode lets us drive the callbacks to completion by
// pumping the instance, turning the request into a blocking call so device
// creation matches the synchronous contract the browser path satisfies via a
// preinitialized device.
typedef struct {
    WGPUAdapter adapter;
    bool        done;
} Adapter_Request;

typedef struct {
    WGPUDevice device;
    bool       done;
} Device_Request;

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                       WGPUStringView message, void* ud1, void* ud2)
{
    (void)message; (void)ud2;
    Adapter_Request* r = ud1;
    if (status == WGPURequestAdapterStatus_Success) r->adapter = adapter;
    r->done = true;
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device,
                      WGPUStringView message, void* ud1, void* ud2)
{
    (void)message; (void)ud2;
    Device_Request* r = ud1;
    if (status == WGPURequestDeviceStatus_Success) r->device = device;
    r->done = true;
}

static WGPUAdapter request_adapter_sync(WGPUInstance instance)
{
    Adapter_Request req = {0};
    WGPURequestAdapterCallbackInfo cb = {
        .mode      = WGPUCallbackMode_AllowProcessEvents,
        .callback  = on_adapter,
        .userdata1 = &req,
    };
    wgpuInstanceRequestAdapter(instance, NULL, cb);
    while (!req.done) wgpuInstanceProcessEvents(instance);
    return req.adapter;
}

static WGPUDevice request_device_sync(WGPUInstance instance, WGPUAdapter adapter)
{
    Device_Request req = {0};
    WGPURequestDeviceCallbackInfo cb = {
        .mode      = WGPUCallbackMode_AllowProcessEvents,
        .callback  = on_device,
        .userdata1 = &req,
    };
    wgpuAdapterRequestDevice(adapter, NULL, cb);
    while (!req.done) wgpuInstanceProcessEvents(instance);
    return req.device;
}

Mel_Gpu_Device* mel_gpu_device_create_opt(Mel_Gpu_Device_Opt opt)
{
    (void)opt;
    WGPUInstance instance = wgpuCreateInstance(NULL);
    if (!instance) return NULL;

    WGPUAdapter adapter = request_adapter_sync(instance);
    if (!adapter) { wgpuInstanceRelease(instance); return NULL; }

    WGPUDevice device = request_device_sync(instance, adapter);
    if (!device) { wgpuAdapterRelease(adapter); wgpuInstanceRelease(instance); return NULL; }

    Mel_Gpu_Device* dev = calloc(1, sizeof *dev);
    if (!dev) { wgpuDeviceRelease(device); wgpuAdapterRelease(adapter); wgpuInstanceRelease(instance); return NULL; }
    dev->instance = instance;
    dev->adapter  = adapter;
    dev->device   = device;
    dev->queue    = wgpuDeviceGetQueue(device);
    return dev;
}

#endif

void mel_gpu_device_destroy(Mel_Gpu_Device* dev)
{
    if (!dev) return;
    if (dev->queue)    wgpuQueueRelease(dev->queue);
    if (dev->device)   wgpuDeviceRelease(dev->device);
    if (dev->adapter)  wgpuAdapterRelease(dev->adapter);
    if (dev->instance) wgpuInstanceRelease(dev->instance);
    free(dev);
}
