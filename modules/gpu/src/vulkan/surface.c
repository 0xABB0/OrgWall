#include "vk_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

#if defined(__linux__)
#include "linux/surface.h"
#endif

Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native)
{
    if (!dev || !native)
        return NULL;

    Mel_Gpu_Surface* s = mel_alloc_type(mel_alloc_heap(), Mel_Gpu_Surface);
    *s = (Mel_Gpu_Surface){ 0 };
    s->instance = dev->instance;
    s->native = native;

#if defined(__APPLE__)
    s->vk = mel_gpu__vk_create_metal_surface(dev->instance->vk, native, &s->metal_layer);
    if (s->vk == VK_NULL_HANDLE)
    {
        mel_log_error("gpu", "failed to create Metal surface");
        mel_dealloc(mel_alloc_heap(), s);
        return NULL;
    }
#elif defined(_WIN32)
    s->vk = mel_gpu__vk_create_win32_surface(dev->instance->vk, native);
    if (s->vk == VK_NULL_HANDLE)
    {
        mel_log_error("gpu", "failed to create Win32 surface");
        mel_dealloc(mel_alloc_heap(), s);
        return NULL;
    }
#elif defined(__ANDROID__)
    s->vk = mel_gpu__vk_create_android_surface(dev->instance->vk, native);
    if (s->vk == VK_NULL_HANDLE)
    {
        mel_log_error("gpu", "failed to create Android surface");
        mel_dealloc(mel_alloc_heap(), s);
        return NULL;
    }
#elif defined(__linux__)
    s->vk = mel_gpu__vk_create_linux_surface(dev->instance->vk, native);
    if (s->vk == VK_NULL_HANDLE)
    {
        mel_log_error("gpu", "failed to create Linux surface");
        mel_dealloc(mel_alloc_heap(), s);
        return NULL;
    }
#else
    mel_log_error("gpu", "surface creation not implemented for this platform");
    mel_dealloc(mel_alloc_heap(), s);
    return NULL;
#endif
    return s;
}

void mel_gpu_surface_destroy(Mel_Gpu_Surface* s)
{
    if (!s)
        return;
    if (s->vk)
        vkDestroySurfaceKHR(s->instance->vk, s->vk, NULL);
#if defined(__APPLE__)
    if (s->metal_layer)
        mel_gpu__vk_metal_layer_release(s->metal_layer);
#endif
    mel_dealloc(mel_alloc_heap(), s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s)
        return;
    s->width = width;
    s->height = height;
#if defined(__APPLE__)
    mel_gpu__vk_metal_layer_set_size(s->metal_layer, width, height);
#endif
}
