#include "vulkan_backend.h"

Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native)
{
    if (!dev || !native)
        return NULL;

    Mel_Gpu_Surface* s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->instance = dev->instance;
    s->native = native;

#if defined(__APPLE__)
    s->metal_layer = mel_gpu__vk_make_metal_layer(native);
    if (!s->metal_layer || mel_gpu__vk_create_metal_surface(dev->instance, s->metal_layer, &s->surface) != VK_SUCCESS)
    {
        mel_gpu_surface_destroy(s);
        return NULL;
    }
#elif defined(__ANDROID__)
    if (mel_gpu__vk_create_android_surface(dev->instance, native, &s->surface) != VK_SUCCESS)
    {
        mel_gpu_surface_destroy(s);
        return NULL;
    }
#else
    free(s);
    return NULL;
#endif
    return s;
}

void mel_gpu_surface_destroy(Mel_Gpu_Surface* s)
{
    if (!s)
        return;
    if (s->surface)
        vkDestroySurfaceKHR(s->instance, s->surface, NULL);
#if defined(__APPLE__)
    if (s->metal_layer)
        mel_gpu__vk_release_metal_layer(s->metal_layer);
#endif
    free(s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s)
        return;
#if defined(__APPLE__)
    mel_gpu__vk_layer_set_size(s->metal_layer, width, height);
#else
    (void)width;
    (void)height;
#endif
}

void mel_gpu_surface_rebuild(Mel_Gpu_Surface* s, void* new_native)
{
    if (!s)
        return;
    if (s->surface)
    {
        vkDestroySurfaceKHR(s->instance, s->surface, NULL);
        s->surface = VK_NULL_HANDLE;
    }
#if defined(__APPLE__)
    if (s->metal_layer)
    {
        mel_gpu__vk_release_metal_layer(s->metal_layer);
        s->metal_layer = NULL;
    }
    s->metal_layer = mel_gpu__vk_make_metal_layer(new_native);
    if (s->metal_layer)
        mel_gpu__vk_create_metal_surface(s->instance, s->metal_layer, &s->surface);
#elif defined(__ANDROID__)
    mel_gpu__vk_create_android_surface(s->instance, new_native, &s->surface);
#endif
    s->native = new_native;
}
