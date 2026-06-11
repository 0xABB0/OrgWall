#include "../wgpu_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native)
{
    if (!dev || !native)
    {
        mel_log_error("gpu", "surface_create: null device or canvas selector");
        return NULL;
    }

    const char* selector = (const char*)native;

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector src = {
        .chain = { .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
        .selector = mel_gpu__sv(selector),
    };
    WGPUSurfaceDescriptor desc = { .nextInChain = &src.chain, .label = mel_gpu__sv("mel-webgpu-surface") };
    WGPUSurface           ws = wgpuInstanceCreateSurface(dev->wgpu_instance, &desc);
    if (!ws)
    {
        mel_log_error("gpu", "surface_create: wgpuInstanceCreateSurface returned null for canvas '%s'", selector);
        return NULL;
    }

    Mel_Gpu_Surface* s = mel_alloc_type(mel_alloc_heap(), Mel_Gpu_Surface);
    *s = (Mel_Gpu_Surface){ 0 };
    s->instance = dev->instance;
    s->wgpu = ws;
    s->layer = NULL;
    s->native = native;
    s->width = 0;
    s->height = 0;
    return s;
}

void mel_gpu_surface_destroy(Mel_Gpu_Surface* s)
{
    if (!s)
        return;
    if (s->wgpu)
        wgpuSurfaceRelease(s->wgpu);
    mel_dealloc(mel_alloc_heap(), s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s)
        return;
    s->width = width;
    s->height = height;
}
