#include "webgpu_backend.h"

WGPUSurface mel_gpu__webgpu_surface_create(WGPUInstance instance, void* native_window)
{
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = {
        .chain    = { .next = NULL, .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
        .selector = mel_gpu__sv((const char*)native_window),
    };
    WGPUSurfaceDescriptor sd = { .nextInChain = &canvas.chain };
    return wgpuInstanceCreateSurface(instance, &sd);
}
