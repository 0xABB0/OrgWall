#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include "webgpu_backend.h"

WGPUSurface mel_gpu__webgpu_surface_create(WGPUInstance instance, void* native_window)
{
    NSView* view = (__bridge NSView*)native_window;
    if (!view) return NULL;

    CAMetalLayer* layer = [CAMetalLayer layer];
    view.wantsLayer = YES;
    view.layer = layer;

    WGPUSurfaceSourceMetalLayer src = {
        .chain = { .next = NULL, .sType = WGPUSType_SurfaceSourceMetalLayer },
        .layer = (__bridge void*)layer,
    };
    WGPUSurfaceDescriptor sd = { .nextInChain = &src.chain };
    return wgpuInstanceCreateSurface(instance, &sd);
}
