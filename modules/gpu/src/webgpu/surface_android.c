#include "webgpu_backend.h"

WGPUSurface mel_gpu__webgpu_surface_create(WGPUInstance instance, void* native_window)
{
    if (!native_window) return NULL;

    WGPUSurfaceSourceAndroidNativeWindow src = {
        .chain  = { .next = NULL, .sType = WGPUSType_SurfaceSourceAndroidNativeWindow },
        .window = native_window,
    };
    WGPUSurfaceDescriptor sd = { .nextInChain = &src.chain };
    return wgpuInstanceCreateSurface(instance, &sd);
}
