#include <stdio.h>

#include "webgpu_backend.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(void, mel_gpu__webgpu_canvas_size, (const char* sel, int w, int h), {
    const el = document.querySelector(UTF8ToString(sel));
    if (el)
    {
        el.width = w;
        el.height = h;
    }
});
#endif

Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native)
{
    if (!dev || !native)
        return NULL;

    WGPUSurface ws = mel_gpu__webgpu_surface_create(dev->instance, native);
    if (!ws)
        return NULL;

    Mel_Gpu_Surface* s = calloc(1, sizeof *s);
    if (!s)
    {
        wgpuSurfaceRelease(ws);
        return NULL;
    }
    s->instance = dev->instance;
    s->surface = ws;
    s->native = native;
#if defined(__EMSCRIPTEN__)
    snprintf(s->selector, sizeof s->selector, "%s", (const char*)native);
#endif
    return s;
}

void mel_gpu_surface_destroy(Mel_Gpu_Surface* s)
{
    if (!s)
        return;
    if (s->surface)
        wgpuSurfaceRelease(s->surface);
    free(s);
}

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height)
{
    if (!s)
        return;
#if defined(__EMSCRIPTEN__)
    mel_gpu__webgpu_canvas_size(s->selector, width, height);
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
        wgpuSurfaceRelease(s->surface);
        s->surface = NULL;
    }
    s->surface = mel_gpu__webgpu_surface_create(s->instance, new_native);
    s->native = new_native;
#if defined(__EMSCRIPTEN__)
    snprintf(s->selector, sizeof s->selector, "%s", (const char*)new_native);
#endif
}
