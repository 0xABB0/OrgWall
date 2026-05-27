#ifdef __APPLE__

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

#include <core/types.h>

void* mel_gpu__vk_make_metal_layer(void* nsview)
{
    NSView* view = (__bridge NSView*)nsview;

    CGFloat scale = 1.0;
    if (view.window)             scale = view.window.backingScaleFactor;
    else if (NSScreen.mainScreen) scale = NSScreen.mainScreen.backingScaleFactor;

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.contentsScale = scale;
    layer.drawableSize  = CGSizeMake(view.bounds.size.width * scale, view.bounds.size.height * scale);

    view.layer      = layer;
    view.wantsLayer = YES;

    return (__bridge_retained void*)layer;
}

void mel_gpu__vk_release_metal_layer(void* layer)
{
    if (layer) CFBridgingRelease(layer);
}

void mel_gpu__vk_layer_set_size(void* layer_ptr, i32 width, i32 height)
{
    if (!layer_ptr) return;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_ptr;
    CGFloat scale = layer.contentsScale > 0 ? layer.contentsScale : 1.0;
    layer.drawableSize = CGSizeMake(width * scale, height * scale);
}

VkResult mel_gpu__vk_create_metal_surface(VkInstance instance, void* layer, VkSurfaceKHR* out_surface)
{
    VkMetalSurfaceCreateInfoEXT ci = {
        .sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pLayer = (__bridge CAMetalLayer*)layer,
    };
    return vkCreateMetalSurfaceEXT(instance, &ci, NULL, out_surface);
}

#endif
