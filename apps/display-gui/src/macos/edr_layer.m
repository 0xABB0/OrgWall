#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "../edr.h"

static NSWindow*            s_window;
static CAMetalLayer*        s_layer;
static id<MTLDevice>        s_device;
static id<MTLCommandQueue>  s_queue;

static void edr_create(void)
{
    s_device = MTLCreateSystemDefaultDevice();
    if (!s_device) return;
    s_queue = [s_device newCommandQueue];

    NSRect rect = NSMakeRect(80, 80, 360, 220);
    s_window = [[NSWindow alloc]
        initWithContentRect:rect
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    s_window.title = @"EDR headroom source (HDR white)";
    s_window.releasedWhenClosed = NO;

    s_layer = [CAMetalLayer layer];
    s_layer.device                          = s_device;
    s_layer.pixelFormat                     = MTLPixelFormatRGBA16Float;
    s_layer.wantsExtendedDynamicRangeContent = YES;
    s_layer.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearDisplayP3);
    s_layer.framebufferOnly = YES;
    s_layer.drawableSize    = CGSizeMake(rect.size.width, rect.size.height);

    NSView* view   = s_window.contentView;
    view.layer      = s_layer;
    view.wantsLayer = YES;
}

static void edr_present(f32 level)
{
    if (!s_layer || !s_queue) return;
    id<CAMetalDrawable> drawable = [s_layer nextDrawable];
    if (!drawable) return;

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture     = drawable.texture;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor  = MTLClearColorMake(level, level, level, 1.0);

    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc endEncoding];
    [cb presentDrawable:drawable];
    [cb commit];
}

void display_edr_set(bool on, f32 level)
{
    if (on) {
        if (!s_window) edr_create();
        if (!s_window) return;
        if (!s_window.isVisible) [s_window makeKeyAndOrderFront:nil];
        edr_present(level);
    } else if (s_window) {
        [s_window orderOut:nil];
    }
}
