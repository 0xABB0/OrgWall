#include "../macos/mtl_backend.h"

#import <UIKit/UIKit.h>

#include <allocator/heap.h>
#include <log/log.h>

Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native)
{
    if (!dev || !native)
    {
        mel_log_error("gpu", "surface_create: null device or native view");
        return NULL;
    }

    UIView* view = (__bridge UIView*)native;
    if (![view isKindOfClass:[UIView class]])
    {
        mel_log_error("gpu", "surface_create: native handle is not a UIView");
        return NULL;
    }

    CGFloat scale = view.window ? view.window.screen.scale : UIScreen.mainScreen.scale;
    if (scale <= 0)
        scale = 1.0;

    CAMetalLayer* layer;
    if ([view.layer isKindOfClass:[CAMetalLayer class]])
    {
        layer = (CAMetalLayer*)view.layer;
    }
    else
    {
        layer = [CAMetalLayer layer];
        layer.frame = view.bounds;
        [view.layer addSublayer:layer];
    }

    layer.device = dev->mtl;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(view.bounds.size.width * scale, view.bounds.size.height * scale);

    Mel_Gpu_Surface* s = mel_calloc(mel_alloc_heap(), sizeof(Mel_Gpu_Surface));
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
