#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

#include <core/types.h>

VkSurfaceKHR mel_gpu__vk_create_metal_surface(VkInstance instance, void* native_view, void** out_layer)
{
    *out_layer = NULL;

    NSView* view = (__bridge NSView*)native_view;

    CGFloat scale = 1.0;
    if (view.window)
        scale = view.window.backingScaleFactor;
    else if (NSScreen.mainScreen)
        scale = NSScreen.mainScreen.backingScaleFactor;

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(view.bounds.size.width * scale, view.bounds.size.height * scale);

    view.layer = layer;
    view.wantsLayer = YES;

    VkMetalSurfaceCreateInfoEXT ci = {
        .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pLayer = layer,
    };

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateMetalSurfaceEXT(instance, &ci, NULL, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    *out_layer = (__bridge_retained void*)layer;
    return surface;
}

void mel_gpu__vk_metal_layer_set_size(void* layer_ptr, i32 width, i32 height)
{
    if (!layer_ptr)
        return;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_ptr;
    CGFloat       scale = layer.contentsScale > 0 ? layer.contentsScale : 1.0;
    layer.drawableSize = CGSizeMake(width * scale, height * scale);
}

void mel_gpu__vk_metal_layer_release(void* layer)
{
    if (layer)
        CFBridgingRelease(layer);
}
