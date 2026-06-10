#include <gpu/render_source.h>

#include <core/platform.h>
#include <time/nano.h>
#include <allocator/allocator.h>
#include <vat/vat.h>
#include <vat/tick.h>
#if MEL_PLATFORM_OSX
#include <vat/vsync.h>
#endif

struct Mel_Gpu_Render_Source
{
    const Mel_Alloc* alloc;
    Mel_Vat_Tick*    tick;
#if MEL_PLATFORM_OSX
    Mel_Vat_Vsync* vsync;
#endif
    Mel_Gpu_Swapchain* sc;
    Mel_Gpu_Render_Fn  fn;
    void*              user;
    u64                last_ns;
};

static void mel_gpu__render_frame(Mel_Gpu_Render_Source* s)
{
    u64 now = mel_nanos_since_unspecified_epoch();
    f64 dt = s->last_ns ? (f64)(now - s->last_ns) / (f64)MEL_NANOS_PER_SEC : 0.0;
    s->last_ns = now;
    if (s->fn)
        s->fn(s->sc, dt, s->user);
}

static bool mel_gpu__render_tick(void* user)
{
    mel_gpu__render_frame(user);
    return true;
}

#if MEL_PLATFORM_OSX
static void mel_gpu__render_vsync(void* user) { mel_gpu__render_frame(user); }
#endif

Mel_Gpu_Render_Source* mel_gpu_render_source_new(Mel_Vat* vat, Mel_Gpu_Swapchain* sc, u32 hz, Mel_Gpu_Render_Fn fn, void* user)
{
    if (!vat || !sc || hz == 0)
        return NULL;

    const Mel_Alloc*       alloc = mel_vat_alloc(vat);
    Mel_Gpu_Render_Source* s = mel_alloc_type(alloc, Mel_Gpu_Render_Source);
    *s = (Mel_Gpu_Render_Source){ 0 };
    s->alloc = alloc;
    s->sc = sc;
    s->fn = fn;
    s->user = user;

#if MEL_PLATFORM_OSX
    s->vsync = mel_vat_vsync_open(vat, alloc, mel_gpu__render_vsync, s);
    if (s->vsync)
        return s;
#endif

    i64 interval = (i64)MEL_NANOS_PER_SEC / (i64)hz;
    s->tick = mel_vat_tick_open(vat, alloc, interval, mel_gpu__render_tick, s);
    if (!s->tick)
    {
        mel_dealloc(alloc, s);
        return NULL;
    }
    return s;
}

void mel_gpu_render_source_destroy(Mel_Gpu_Render_Source* s)
{
    if (!s)
        return;
#if MEL_PLATFORM_OSX
    if (s->vsync)
        mel_vat_vsync_close(s->vsync);
#endif
    if (s->tick)
        mel_vat_tick_close(s->tick);
    mel_dealloc(s->alloc, s);
}
