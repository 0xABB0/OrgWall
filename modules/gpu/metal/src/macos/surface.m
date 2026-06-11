#include "mtl_backend.h"

#import <AppKit/AppKit.h>

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
    layer.device = dev->mtl;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(view.bounds.size.width * scale, view.bounds.size.height * scale);

    view.layer = layer;
    view.wantsLayer = YES;

    Mel_Gpu_Surface* s = mel_alloc_type(mel_alloc_heap(), Mel_Gpu_Surface);
    *s = (Mel_Gpu_Surface){ 0 };
    s->instance = dev->instance;
    s->layer = layer;
    s->native = native;
    s->width = (i32)(view.bounds.size.width * scale);
    s->height = (i32)(view.bounds.size.height * scale);
    return s;
}

void mel_gpu_surface_destroy(Mel_Gpu_Surface* s)
{
    if (!s)
        return;
    s->layer = nil;
    mel_dealloc(mel_alloc_heap(), s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s || !s->layer)
        return;
    CGFloat scale = s->layer.contentsScale > 0 ? s->layer.contentsScale : 1.0;
    s->layer.drawableSize = CGSizeMake(width * scale, height * scale);
    s->width = (i32)(width * scale);
    s->height = (i32)(height * scale);
}
