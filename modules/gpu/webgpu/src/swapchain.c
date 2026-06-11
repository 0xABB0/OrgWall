#include "wgpu_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

static void mel_gpu__swapchain_configure(Mel_Gpu_Swapchain* sc)
{
    WGPUSurfaceConfiguration cfg = {
        .device = sc->dev->wgpu,
        .format = sc->format,
        .usage = WGPUTextureUsage_RenderAttachment,
        .width = sc->width,
        .height = sc->height,
        .alphaMode = WGPUCompositeAlphaMode_Auto,
        .presentMode = sc->present_mode,
    };
    wgpuSurfaceConfigure(sc->surface->wgpu, &cfg);
    sc->configured = true;
}

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.surface || !opt.surface->wgpu)
    {
        mel_log_error("gpu", "swapchain_create: null device or surface");
        return NULL;
    }

    Mel_Gpu_Swapchain* sc = mel_alloc_type(mel_alloc_heap(), Mel_Gpu_Swapchain);
    *sc = (Mel_Gpu_Swapchain){ 0 };
    sc->dev = dev;
    sc->surface = opt.surface;
    sc->vsync = opt.vsync;
    sc->present_mode = opt.vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;
    sc->recorder.dev = dev;
    sc->recorder.sc = sc;

    WGPUTextureFormat want = mel_gpu__wgpu_format(opt.format);
    sc->format = want != WGPUTextureFormat_Undefined ? want : WGPUTextureFormat_BGRA8Unorm;

    sc->width = opt.width > 0 ? (u32)opt.width : (u32)(opt.surface->width > 0 ? opt.surface->width : 1);
    sc->height = opt.height > 0 ? (u32)opt.height : (u32)(opt.surface->height > 0 ? opt.surface->height : 1);

    mel_gpu__swapchain_configure(sc);

    mel_log_info("gpu", "webgpu swapchain ready: %ux%u, format %d, vsync=%d", sc->width, sc->height, (int)sc->format, (int)opt.vsync);
    return sc;
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc || !sc->surface)
        return;
    sc->width = width > 0 ? (u32)width : sc->width;
    sc->height = height > 0 ? (u32)height : sc->height;
    mel_gpu__swapchain_configure(sc);
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc)
        return;
    if (sc->frame_view)
        wgpuTextureViewRelease(sc->frame_view);
    if (sc->frame_texture)
        wgpuTextureRelease(sc->frame_texture);
    if (sc->configured && sc->surface && sc->surface->wgpu)
        wgpuSurfaceUnconfigure(sc->surface->wgpu);
    mel_dealloc(mel_alloc_heap(), sc);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc) { return sc ? mel_gpu__wgpu_format_to_mel(sc->format) : MEL_GPU_FORMAT_UNDEFINED; }

Mel_Gpu_Swapchain_Extent mel_gpu_swapchain_extent(const Mel_Gpu_Swapchain* sc)
{
    if (!sc)
        return (Mel_Gpu_Swapchain_Extent){ 0, 0 };
    return (Mel_Gpu_Swapchain_Extent){ sc->width, sc->height };
}
