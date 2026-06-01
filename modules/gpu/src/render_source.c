#include <gpu/render_source.h>

#include <reactor/reactor.h>
#include <time/nano.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

struct Mel_Gpu_Render_Source
{
    Mel_Reactor_Source* timer;
    Mel_Gpu_Swapchain*  sc;
    Mel_Gpu_Render_Fn   fn;
    void*               user;
    u64                 last_ns;
};

static bool mel_gpu__render_tick(void* user)
{
    Mel_Gpu_Render_Source* s = user;
    u64                    now = mel_nanos_since_unspecified_epoch();
    f64                    dt = s->last_ns ? (f64)(now - s->last_ns) / (f64)MEL_NANOS_PER_SEC : 0.0;
    s->last_ns = now;
    if (s->fn)
        s->fn(s->sc, dt, s->user);
    return true;
}

Mel_Gpu_Render_Source* mel_gpu_render_source_new(Mel_Reactor* reactor, Mel_Gpu_Swapchain* sc, u32 hz, Mel_Gpu_Render_Fn fn, void* user)
{
    if (!reactor || !sc || hz == 0)
        return NULL;

    Mel_Gpu_Render_Source* s = mel_alloc_type(mel_alloc_heap(), Mel_Gpu_Render_Source);
    *s = (Mel_Gpu_Render_Source){ 0 };
    s->sc = sc;
    s->fn = fn;
    s->user = user;

    i64 interval = (i64)MEL_NANOS_PER_SEC / (i64)hz;
    s->timer = mel_reactor_timer_new(interval, mel_gpu__render_tick, s);
    if (!s->timer)
    {
        mel_dealloc(mel_alloc_heap(), s);
        return NULL;
    }
    mel_reactor_source_attach(reactor, s->timer);
    return s;
}

void mel_gpu_render_source_destroy(Mel_Gpu_Render_Source* s)
{
    if (!s)
        return;
    if (s->timer)
        mel_reactor_source_destroy(s->timer);
    mel_dealloc(mel_alloc_heap(), s);
}
