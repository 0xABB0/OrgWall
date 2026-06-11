#include "d3d_backend.h"

#include <gpu/surface.h>

#include <log/log.h>

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
    mel_dealloc(s->instance->alloc, s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s)
        return;
    s->width = width;
    s->height = height;
}
