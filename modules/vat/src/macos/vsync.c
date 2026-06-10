#include <vat/vsync.h>

#include <allocator/allocator.h>
#include <debug/assert.h>

#include <CoreVideo/CoreVideo.h>
#include <stdatomic.h>

struct Mel_Vat_Vsync
{
    const Mel_Alloc* alloc;
    Mel_Vat*         vat;
    Mel_Vat_Source*  source;
    Mel_Vat_Vsync_Fn fn;
    void*            user;
    CVDisplayLinkRef link;
    atomic_bool      signaled;
    _Atomic(i64)     interval;
};

static i64 mel_vat__vsync_deadline(Mel_Vat_Source* source)
{
    Mel_Vat_Vsync* v = mel_vat_source_state(source);
    return atomic_load_explicit(&v->signaled, memory_order_acquire) ? 0 : MEL_VAT_NEVER;
}

static bool mel_vat__vsync_drain(Mel_Vat_Source* source, u32 budget)
{
    MEL_UNUSED(budget);
    Mel_Vat_Vsync* v = mel_vat_source_state(source);
    atomic_store_explicit(&v->signaled, false, memory_order_release);
    v->fn(v->user);
    return false;
}

static const Mel_Vat_Source_Vtbl mel_vat__vsync_vtbl = {
    .wakeables = NULL,
    .deadline = mel_vat__vsync_deadline,
    .drain = mel_vat__vsync_drain,
    .cancel = NULL,
};

static CVReturn mel_vat__vsync_cb(CVDisplayLinkRef link, const CVTimeStamp* now, const CVTimeStamp* output, CVOptionFlags flags_in, CVOptionFlags* flags_out, void* ctx)
{
    MEL_UNUSED(link);
    MEL_UNUSED(now);
    MEL_UNUSED(flags_in);
    MEL_UNUSED(flags_out);
    Mel_Vat_Vsync* v = ctx;
    if (output->videoTimeScale != 0 && output->videoRefreshPeriod != 0)
        atomic_store_explicit(&v->interval, (i64)((double)output->videoRefreshPeriod / (double)output->videoTimeScale * 1e9), memory_order_relaxed);
    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(&v->signaled, &expected, true, memory_order_acq_rel, memory_order_acquire))
    {
        Mel_Vat_Waiter* waiter = mel_vat_waiter(v->vat);
        waiter->vt->ring(waiter);
    }
    return kCVReturnSuccess;
}

Mel_Vat_Vsync* mel_vat_vsync_open(Mel_Vat* vat, const Mel_Alloc* alloc, Mel_Vat_Vsync_Fn fn, void* user)
{
    mel_assert(vat != NULL);
    mel_assert(alloc != NULL);
    mel_assert(fn != NULL);
    Mel_Vat_Vsync* v = mel_alloc_type(alloc, Mel_Vat_Vsync);
    v->alloc = alloc;
    v->vat = vat;
    v->fn = fn;
    v->user = user;
    v->link = NULL;
    atomic_init(&v->signaled, false);
    atomic_init(&v->interval, 0);
    if (CVDisplayLinkCreateWithActiveCGDisplays(&v->link) != kCVReturnSuccess || v->link == NULL)
    {
        mel_dealloc(alloc, v);
        return NULL;
    }
    CVDisplayLinkSetOutputCallback(v->link, mel_vat__vsync_cb, v);
    if (CVDisplayLinkStart(v->link) != kCVReturnSuccess)
    {
        CVDisplayLinkRelease(v->link);
        mel_dealloc(alloc, v);
        return NULL;
    }
    v->source = mel_vat_source_open(vat, &mel_vat__vsync_vtbl, v);
    return v;
}

void mel_vat_vsync_close(Mel_Vat_Vsync* vsync)
{
    if (vsync == NULL)
        return;
    CVDisplayLinkStop(vsync->link);
    CVDisplayLinkRelease(vsync->link);
    mel_vat_source_close(vsync->source);
    mel_dealloc(vsync->alloc, vsync);
}

i64 mel_vat_vsync_interval(const Mel_Vat_Vsync* vsync) { return vsync != NULL ? atomic_load_explicit(&vsync->interval, memory_order_relaxed) : 0; }
