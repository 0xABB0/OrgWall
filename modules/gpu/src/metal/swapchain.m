#include "metal.h"

#import <AppKit/AppKit.h>

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.surface || !opt.surface->layer)
        return NULL;

    Mel_Gpu_Format fmt = opt.format == MEL_GPU_FORMAT_UNDEFINED ? MEL_GPU_FORMAT_BGRA8_UNORM : opt.format;

    opt.surface->layer.pixelFormat = mel_gpu__mtl_pixel_format(fmt);

    Mel_Gpu_Swapchain* sc = calloc(1, sizeof *sc);
    if (!sc)
        return NULL;
    sc->device = dev;
    sc->surface = opt.surface;
    sc->format = fmt;
    sc->width = opt.width;
    sc->height = opt.height;
    sc->cmd.swapchain = sc;

    mel_gpu_surface_reconfigure(opt.surface, opt.width, opt.height);
    return sc;
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc)
        return;
    sc->cmd_buffer = nil;
    sc->drawable = nil;
    sc->surface = NULL;
    free(sc);
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc)
        return;
    sc->width = width;
    sc->height = height;
    mel_gpu_surface_reconfigure(sc->surface, width, height);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc) { return sc ? sc->format : MEL_GPU_FORMAT_UNDEFINED; }
