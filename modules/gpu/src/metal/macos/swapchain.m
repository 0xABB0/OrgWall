#include "mtl_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.surface || !opt.surface->layer)
    {
        mel_log_error("gpu", "swapchain_create: null device or surface");
        return NULL;
    }

    Mel_Gpu_Swapchain* sc = mel_calloc(mel_alloc_heap(), sizeof(Mel_Gpu_Swapchain));
    sc->dev = dev;
    sc->surface = opt.surface;
    sc->vsync = opt.vsync;
    sc->recorder.dev = dev;
    sc->recorder.sc = sc;

    MTLPixelFormat want = mel_gpu__mtl_format(opt.format);
    sc->format = want != MTLPixelFormatInvalid ? want : MTLPixelFormatBGRA8Unorm;
    opt.surface->layer.pixelFormat = sc->format;

#if TARGET_OS_OSX
    if (@available(macOS 10.13, *))
        opt.surface->layer.displaySyncEnabled = opt.vsync;
#else
    if (!opt.vsync)
        mel_log_warn("gpu", "swapchain_create: vsync=off requested but iOS CAMetalLayer has no displaySyncEnabled; presentation stays vsynced");
#endif

    CGSize ds = opt.surface->layer.drawableSize;
    sc->width = (u32)ds.width;
    sc->height = (u32)ds.height;

    mel_log_info("gpu", "metal swapchain ready: %ux%u, format %lu, vsync=%d", sc->width, sc->height, (unsigned long)sc->format, (int)opt.vsync);
    return sc;
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc || !sc->surface || !sc->surface->layer)
        return;
    CGFloat scale = sc->surface->layer.contentsScale > 0 ? sc->surface->layer.contentsScale : 1.0;
    sc->surface->layer.drawableSize = CGSizeMake(width * scale, height * scale);
    CGSize ds = sc->surface->layer.drawableSize;
    sc->width = (u32)ds.width;
    sc->height = (u32)ds.height;
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc)
        return;
    sc->drawable = nil;
    mel_dealloc(mel_alloc_heap(), sc);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc) { return sc ? mel_gpu__mtl_format_to_mel(sc->format) : MEL_GPU_FORMAT_UNDEFINED; }

Mel_Gpu_Swapchain_Extent mel_gpu_swapchain_extent(const Mel_Gpu_Swapchain* sc)
{
    if (!sc)
        return (Mel_Gpu_Swapchain_Extent){ 0, 0 };
    return (Mel_Gpu_Swapchain_Extent){ sc->width, sc->height };
}
