#include "metal.h"

#import <AppKit/AppKit.h>

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.native_window) return NULL;

    NSView* view = (__bridge NSView*)opt.native_window;
    Mel_Gpu_Format fmt = opt.format == MEL_GPU_FORMAT_UNDEFINED ? MEL_GPU_FORMAT_BGRA8_UNORM : opt.format;

    CGFloat scale = 1.0;
    if (view.window)            scale = view.window.backingScaleFactor;
    else if (NSScreen.mainScreen) scale = NSScreen.mainScreen.backingScaleFactor;

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device         = dev->mtl;
    layer.pixelFormat    = mel_gpu__mtl_pixel_format(fmt);
    layer.framebufferOnly = YES;
    layer.contentsScale  = scale;
    layer.drawableSize   = CGSizeMake(opt.width * scale, opt.height * scale);

    // Assign the layer before wantsLayer to make this an app-owned (layer-hosting)
    // view: AppKit then leaves our CAMetalLayer in place instead of creating one.
    view.layer      = layer;
    view.wantsLayer = YES;

    Mel_Gpu_Swapchain* sc = calloc(1, sizeof *sc);
    if (!sc) return NULL;
    sc->device        = dev;
    sc->layer         = layer;
    sc->format        = fmt;
    sc->width         = opt.width;
    sc->height        = opt.height;
    sc->cmd.swapchain = sc;
    return sc;
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc) return;
    sc->cmd_buffer = nil;
    sc->drawable   = nil;
    sc->layer      = nil;
    free(sc);
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc) return;
    sc->width  = width;
    sc->height = height;
    CGFloat scale = sc->layer.contentsScale > 0 ? sc->layer.contentsScale : 1.0;
    sc->layer.drawableSize = CGSizeMake(width * scale, height * scale);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc)
{
    return sc ? sc->format : MEL_GPU_FORMAT_UNDEFINED;
}
