#include "metal.h"

Mel_Gpu_Device* mel_gpu_device_create_opt(Mel_Gpu_Device_Opt opt)
{
    (void)opt;
    id<MTLDevice> mtl = MTLCreateSystemDefaultDevice();
    if (!mtl) return NULL;

    Mel_Gpu_Device* dev = calloc(1, sizeof *dev);
    if (!dev) return NULL;
    dev->mtl   = mtl;
    dev->queue = [mtl newCommandQueue];
    return dev;
}

void mel_gpu_device_destroy(Mel_Gpu_Device* dev)
{
    if (!dev) return;
    dev->queue = nil;
    dev->mtl   = nil;
    free(dev);
}
