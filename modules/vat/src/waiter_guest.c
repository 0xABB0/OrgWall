#include <vat/vat.h>

#include <allocator/allocator.h>
#include <collection/list.h>
#include <debug/assert.h>

typedef struct
{
    Mel_Vat_Waiter    base;
    const Mel_Alloc*  alloc;
    Mel_Vat_Embedder* embedder;
} Guest_Waiter;

static bool guest_arm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    MEL_UNUSED(waiter);
    MEL_UNUSED(wakeable);
    return false;
}

static void guest_disarm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    MEL_UNUSED(waiter);
    MEL_UNUSED(wakeable);
}

static i32 guest_wait(Mel_Vat_Waiter* waiter, i64 timeout_ns)
{
    Guest_Waiter* w = mel_container_of(waiter, Guest_Waiter, base);
    if (timeout_ns == 0)
        w->embedder->vt->schedule_work(w->embedder);
    else
        w->embedder->vt->schedule_delayed_work(w->embedder, timeout_ns);
    return 0;
}

static void guest_ring(Mel_Vat_Waiter* waiter)
{
    Guest_Waiter* w = mel_container_of(waiter, Guest_Waiter, base);
    w->embedder->vt->schedule_work(w->embedder);
}

static void guest_close(Mel_Vat_Waiter* waiter)
{
    Guest_Waiter* w = mel_container_of(waiter, Guest_Waiter, base);
    if (w->embedder->vt->close != NULL)
        w->embedder->vt->close(w->embedder);
    mel_dealloc(w->alloc, w);
}

static const Mel_Vat_Waiter_Vtbl guest_vtbl = { guest_arm, guest_disarm, guest_wait, guest_ring, guest_close };

Mel_Vat_Waiter* mel_vat_waiter_guest(const Mel_Alloc* alloc, Mel_Vat_Embedder* embedder)
{
    mel_assert(alloc != NULL);
    mel_assert(embedder != NULL);
    mel_assert(embedder->vt != NULL);
    mel_assert(embedder->vt->schedule_work != NULL);
    mel_assert(embedder->vt->schedule_delayed_work != NULL);
    Guest_Waiter* w = mel_alloc_type(alloc, Guest_Waiter);
    w->base.vt = &guest_vtbl;
    w->alloc = alloc;
    w->embedder = embedder;
    return &w->base;
}
