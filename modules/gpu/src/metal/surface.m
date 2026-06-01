#include "metal.h"

#import <AppKit/AppKit.h>

static CAMetalLayer* attach_layer(Mel_Gpu_Device* dev, void* native)
{
    NSView* view = (__bridge NSView*)native;
    if (!view)
        return nil;

    CGFloat scale = 1.0;
    if (view.window)
        scale = view.window.backingScaleFactor;
    else if (NSScreen.mainScreen)
        scale = NSScreen.mainScreen.backingScaleFactor;

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = dev->mtl;
    layer.framebufferOnly = YES;
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(view.bounds.size.width * scale, view.bounds.size.height * scale);

    view.layer = layer;
    view.wantsLayer = YES;
    return layer;
}

Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native)
{
    if (!dev || !native)
        return NULL;

    CAMetalLayer* layer = attach_layer(dev, native);
    if (!layer)
        return NULL;

    Mel_Gpu_Surface* s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->device = dev;
    s->layer = layer;
    s->native = native;
    return s;
}

void mel_gpu_surface_destroy(Mel_Gpu_Surface* s)
{
    if (!s)
        return;
    s->layer = nil;
    free(s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s || !s->layer)
        return;
    CGFloat scale = s->layer.contentsScale > 0 ? s->layer.contentsScale : 1.0;
    s->layer.drawableSize = CGSizeMake(width * scale, height * scale);
}

void mel_gpu_surface_rebuild(Mel_Gpu_Surface* s, void* new_native)
{
    if (!s)
        return;
    s->layer = attach_layer(s->device, new_native);
    s->native = new_native;
}
