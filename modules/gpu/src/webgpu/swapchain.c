#include <stdio.h>

#include <emscripten.h>

#include "webgpu_backend.h"

EM_JS(void, mel_gpu__webgpu_canvas_size, (const char* sel, int w, int h), {
    const el = document.querySelector(UTF8ToString(sel));
    if (el) { el.width = w; el.height = h; }
});

static void configure(Mel_Gpu_Swapchain* sc)
{
    mel_gpu__webgpu_canvas_size(sc->selector, sc->width, sc->height);

    WGPUSurfaceConfiguration cfg = {
        .device      = sc->device->device,
        .format      = sc->format,
        .usage       = WGPUTextureUsage_RenderAttachment,
        .width       = (u32)sc->width,
        .height      = (u32)sc->height,
        .alphaMode   = WGPUCompositeAlphaMode_Auto,
        .presentMode = WGPUPresentMode_Fifo,
    };
    wgpuSurfaceConfigure(sc->surface, &cfg);
}

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.native_window) return NULL;

    Mel_Gpu_Swapchain* sc = calloc(1, sizeof *sc);
    if (!sc) return NULL;
    sc->device     = dev;
    sc->mel_format = opt.format == MEL_GPU_FORMAT_UNDEFINED ? MEL_GPU_FORMAT_BGRA8_UNORM : opt.format;
    sc->format     = mel_gpu__wgpu_color_format(sc->mel_format);
    sc->width      = opt.width  > 0 ? opt.width  : 1;
    sc->height     = opt.height > 0 ? opt.height : 1;
    sc->cmd.swapchain = sc;
    snprintf(sc->selector, sizeof sc->selector, "%s", (const char*)opt.native_window);

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = {
        .chain    = { .next = NULL, .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
        .selector = mel_gpu__sv(sc->selector),
    };
    WGPUSurfaceDescriptor sd = { .nextInChain = &canvas.chain };
    sc->surface = wgpuInstanceCreateSurface(dev->instance, &sd);
    if (!sc->surface) { free(sc); return NULL; }

    configure(sc);
    return sc;
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc) return;
    if (sc->cur_view)    wgpuTextureViewRelease(sc->cur_view);
    if (sc->cur_texture) wgpuTextureRelease(sc->cur_texture);
    if (sc->surface)     wgpuSurfaceRelease(sc->surface);
    free(sc);
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc || width <= 0 || height <= 0) return;
    sc->width  = width;
    sc->height = height;
    configure(sc);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc)
{
    return sc ? sc->mel_format : MEL_GPU_FORMAT_UNDEFINED;
}
