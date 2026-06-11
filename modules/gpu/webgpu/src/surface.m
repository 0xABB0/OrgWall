#include "wgpu_backend.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <allocator/heap.h>
#include <log/log.h>

Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native)
{
    if (!dev || !native)
    {
        mel_log_error("gpu", "surface_create: null device or native view");
        return NULL;
    }

    NSView* view = (__bridge NSView*)native;

    CGFloat scale = 1.0;
    if (view.window)
        scale = view.window.backingScaleFactor;
    else if (NSScreen.mainScreen)
        scale = NSScreen.mainScreen.backingScaleFactor;

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(view.bounds.size.width * scale, view.bounds.size.height * scale);

    view.layer = layer;
    view.wantsLayer = YES;

    WGPUSurfaceSourceMetalLayer src = {
        .chain = { .sType = WGPUSType_SurfaceSourceMetalLayer },
        .layer = (__bridge void*)layer,
    };
    WGPUSurfaceDescriptor desc = { .nextInChain = &src.chain, .label = mel_gpu__sv("mel-webgpu-surface") };
    WGPUSurface           ws = wgpuInstanceCreateSurface(dev->wgpu_instance, &desc);
    if (!ws)
    {
        mel_log_error("gpu", "surface_create: wgpuInstanceCreateSurface returned null");
        return NULL;
    }

    Mel_Gpu_Surface* s = mel_alloc_type(mel_alloc_heap(), Mel_Gpu_Surface);
    *s = (Mel_Gpu_Surface){ 0 };
    s->instance = dev->instance;
    s->wgpu = ws;
    s->layer = (__bridge_retained void*)layer;
    s->native = native;
    s->width = (i32)(view.bounds.size.width * scale);
    s->height = (i32)(view.bounds.size.height * scale);
    return s;
}

void mel_gpu_surface_destroy(Mel_Gpu_Surface* s)
{
    if (!s)
        return;
    if (s->wgpu)
        wgpuSurfaceRelease(s->wgpu);
    if (s->layer)
    {
        CAMetalLayer* layer = (__bridge_transfer CAMetalLayer*)s->layer;
        (void)layer;
        s->layer = NULL;
    }
    mel_dealloc(mel_alloc_heap(), s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s || !s->layer)
        return;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)s->layer;
    CGFloat       scale = layer.contentsScale > 0 ? layer.contentsScale : 1.0;
    layer.drawableSize = CGSizeMake(width * scale, height * scale);
    s->width = (i32)(width * scale);
    s->height = (i32)(height * scale);
}
