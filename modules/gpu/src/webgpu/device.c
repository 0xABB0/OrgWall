#include "webgpu_backend.h"

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

void mel_gpu_device_destroy(Mel_Gpu_Device* dev)
{
    if (!dev) return;
    if (dev->queue)    wgpuQueueRelease(dev->queue);
    if (dev->device)   wgpuDeviceRelease(dev->device);
    if (dev->instance) wgpuInstanceRelease(dev->instance);
    free(dev);
}
