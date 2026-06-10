#include <vat/vat.h>

#include <allocator/allocator.h>
#include <collection/list.h>
#include <debug/assert.h>

#include <errno.h>
#include <stdatomic.h>
#include <sys/event.h>
#include <unistd.h>

#define KQ_DOORBELL_IDENT 1
#define KQ_BATCH          256

typedef struct
{
    Mel_Vat_Waiter   base;
    const Mel_Alloc* alloc;
    int              kq;
    atomic_bool      rung;
} Kqueue_Waiter;

static bool kq_arm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    Kqueue_Waiter* w = mel_container_of(waiter, Kqueue_Waiter, base);
    struct kevent  ev[2];
    int            n = 0;
    if (wakeable->events & MEL_VAT_WAKE_IN)
        EV_SET(&ev[n++], (uintptr_t)wakeable->handle, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, wakeable);
    if (wakeable->events & MEL_VAT_WAKE_OUT)
        EV_SET(&ev[n++], (uintptr_t)wakeable->handle, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, wakeable);
    if (n == 0)
        return true;
    return kevent(w->kq, ev, n, NULL, 0, NULL) == 0;
}

static void kq_disarm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    Kqueue_Waiter* w = mel_container_of(waiter, Kqueue_Waiter, base);
    struct kevent  ev[2];
    int            n = 0;
    if (wakeable->events & MEL_VAT_WAKE_IN)
        EV_SET(&ev[n++], (uintptr_t)wakeable->handle, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    if (wakeable->events & MEL_VAT_WAKE_OUT)
        EV_SET(&ev[n++], (uintptr_t)wakeable->handle, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    if (n > 0)
        kevent(w->kq, ev, n, NULL, 0, NULL);
}

static i32 kq_wait(Mel_Vat_Waiter* waiter, i64 timeout_ns)
{
    Kqueue_Waiter* w = mel_container_of(waiter, Kqueue_Waiter, base);
    atomic_store_explicit(&w->rung, false, memory_order_seq_cst);
    struct timespec  ts;
    struct timespec* to = NULL;
    if (timeout_ns >= 0)
    {
        ts.tv_sec = (time_t)(timeout_ns / 1000000000);
        ts.tv_nsec = (long)(timeout_ns % 1000000000);
        to = &ts;
    }
    struct kevent events[KQ_BATCH];
    int           n = kevent(w->kq, NULL, 0, events, KQ_BATCH, to);
    if (n < 0)
        return errno == EINTR ? 0 : -1;
    i32 woke = 0;
    for (int i = 0; i < n; i++)
    {
        Mel_Vat_Wakeable* wk = events[i].udata;
        if (wk == NULL)
            continue;
        u32 bit = 0;
        if (events[i].filter == EVFILT_READ)
            bit = MEL_VAT_WAKE_IN;
        else if (events[i].filter == EVFILT_WRITE)
            bit = MEL_VAT_WAKE_OUT;
        if (events[i].flags & EV_EOF)
            bit |= MEL_VAT_WAKE_HUP;
        if (events[i].flags & EV_ERROR)
            bit |= MEL_VAT_WAKE_ERR;
        wk->revents |= bit;
        woke++;
    }
    return woke;
}

static void kq_ring(Mel_Vat_Waiter* waiter)
{
    Kqueue_Waiter* w = mel_container_of(waiter, Kqueue_Waiter, base);
    if (atomic_exchange_explicit(&w->rung, true, memory_order_seq_cst))
        return;
    struct kevent ev;
    EV_SET(&ev, KQ_DOORBELL_IDENT, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    kevent(w->kq, &ev, 1, NULL, 0, NULL);
}

static void kq_close(Mel_Vat_Waiter* waiter)
{
    Kqueue_Waiter* w = mel_container_of(waiter, Kqueue_Waiter, base);
    if (w->kq >= 0)
        close(w->kq);
    mel_dealloc(w->alloc, w);
}

static const Mel_Vat_Waiter_Vtbl kq_vtbl = { kq_arm, kq_disarm, kq_wait, kq_ring, kq_close };

Mel_Vat_Waiter* mel_vat_waiter_kqueue(const Mel_Alloc* alloc)
{
    mel_assert(alloc != NULL);
    int kq = kqueue();
    if (kq < 0)
        return NULL;
    Kqueue_Waiter* w = mel_alloc_type(alloc, Kqueue_Waiter);
    w->base.vt = &kq_vtbl;
    w->alloc = alloc;
    w->kq = kq;
    atomic_init(&w->rung, false);
    struct kevent ev;
    EV_SET(&ev, KQ_DOORBELL_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    kevent(w->kq, &ev, 1, NULL, 0, NULL);
    return &w->base;
}
