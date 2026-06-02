#include "d3d_backend.h"

#include <gpu/surface.h>

#include <log/log.h>

// U18 (gpu-rhi.md §7.4): the D3D12 surface lowering. Unlike Vulkan — where a VkSurfaceKHR is a concrete
// object created from the HWND — DXGI's swapchain is created directly over the HWND at swapchain-create
// (CreateSwapChainForHwnd), so the surface is a thin record of the native window handle + its extent. The
// `native` is the window module's HWND (modules/window/src/win32 stores the HWND as the node's native), the
// same handle the Vulkan win32 surface consumes.
Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native)
{
    if (!dev || !native)
    {
        mel_log_error("gpu", "surface_create: null device or native window handle");
        return NULL;
    }
    Mel_Gpu_Surface* s = mel_alloc_type(dev->alloc, Mel_Gpu_Surface);
    *s = (Mel_Gpu_Surface){ .instance = dev->instance, .hwnd = native };
    return s;
}

void mel_gpu_surface_destroy(Mel_Gpu_Surface* s)
{
    if (!s || !s->instance)
        return;
    // The HWND is owned by the window module, never by the surface — only the record is freed.
    mel_dealloc(s->instance->alloc, s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s)
        return;
    s->width = width;
    s->height = height;
}
