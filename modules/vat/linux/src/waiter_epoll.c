#include <vat/vat.h>

#include <allocator/allocator.h>
#include <collection/list.h>
#include <debug/assert.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define EP_BATCH 256

typedef struct
{
    Mel_Vat_Waiter   base;
    const Mel_Alloc* alloc;
    int              ep;
    int              doorbell;
    atomic_bool      rung;
} Epoll_Waiter;

static u32 ep_interest(u32 events)
{
    u32 mask = 0;
    if (events & MEL_VAT_WAKE_IN)
        mask |= EPOLLIN;
    if (events & MEL_VAT_WAKE_OUT)
        mask |= EPOLLOUT;
    return mask;
}

static bool ep_arm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    Epoll_Waiter*      w = mel_container_of(waiter, Epoll_Waiter, base);
    struct epoll_event ev;
    ev.events = ep_interest(wakeable->events);
    ev.data.ptr = wakeable;
    if (epoll_ctl(w->ep, EPOLL_CTL_ADD, (int)wakeable->handle, &ev) == 0)
        return true;
    if (errno == EEXIST)
        return epoll_ctl(w->ep, EPOLL_CTL_MOD, (int)wakeable->handle, &ev) == 0;
    return false;
}

static void ep_disarm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    Epoll_Waiter* w = mel_container_of(waiter, Epoll_Waiter, base);
    epoll_ctl(w->ep, EPOLL_CTL_DEL, (int)wakeable->handle, NULL);
}

static int ep_timeout_ms(i64 timeout_ns)
{
    if (timeout_ns < 0)
        return -1;
    if (timeout_ns == 0)
        return 0;
    i64 ms = (timeout_ns + 999999) / 1000000;
    if (ms > INT32_MAX)
        return INT32_MAX;
    return (int)ms;
}

static i32 ep_wait(Mel_Vat_Waiter* waiter, i64 timeout_ns)
{
    Epoll_Waiter* w = mel_container_of(waiter, Epoll_Waiter, base);
    atomic_store_explicit(&w->rung, false, memory_order_seq_cst);
    struct epoll_event events[EP_BATCH];
    int                n = epoll_wait(w->ep, events, EP_BATCH, ep_timeout_ms(timeout_ns));
    if (n < 0)
        return errno == EINTR ? 0 : -1;
    i32 woke = 0;
    for (int i = 0; i < n; i++)
    {
        if (events[i].data.ptr == NULL)
        {
            uint64_t drain;
            while (read(w->doorbell, &drain, sizeof drain) == (ssize_t)sizeof drain)
            {
            }
            woke++;
            continue;
        }
        Mel_Vat_Wakeable* wk = events[i].data.ptr;
        u32               bit = 0;
        if (events[i].events & EPOLLIN)
            bit |= MEL_VAT_WAKE_IN;
        if (events[i].events & EPOLLOUT)
            bit |= MEL_VAT_WAKE_OUT;
        if (events[i].events & EPOLLHUP)
            bit |= MEL_VAT_WAKE_HUP;
        if (events[i].events & EPOLLERR)
            bit |= MEL_VAT_WAKE_ERR;
        wk->revents |= bit;
        woke++;
    }
    return woke;
}

static void ep_ring(Mel_Vat_Waiter* waiter)
{
    Epoll_Waiter* w = mel_container_of(waiter, Epoll_Waiter, base);
    if (atomic_exchange_explicit(&w->rung, true, memory_order_seq_cst))
        return;
    uint64_t one = 1;
    ssize_t  r = write(w->doorbell, &one, sizeof one);
    MEL_UNUSED(r);
}

static void ep_close(Mel_Vat_Waiter* waiter)
{
    Epoll_Waiter* w = mel_container_of(waiter, Epoll_Waiter, base);
    if (w->doorbell >= 0)
        close(w->doorbell);
    if (w->ep >= 0)
        close(w->ep);
    mel_dealloc(w->alloc, w);
}

static const Mel_Vat_Waiter_Vtbl ep_vtbl = { ep_arm, ep_disarm, ep_wait, ep_ring, ep_close };

Mel_Vat_Waiter* mel_vat_waiter_epoll(const Mel_Alloc* alloc)
{
    mel_assert(alloc != NULL);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0)
        return NULL;
    int doorbell = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (doorbell < 0)
    {
        close(ep);
        return NULL;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    if (epoll_ctl(ep, EPOLL_CTL_ADD, doorbell, &ev) != 0)
    {
        close(doorbell);
        close(ep);
        return NULL;
    }
    Epoll_Waiter* w = mel_alloc_type(alloc, Epoll_Waiter);
    w->base.vt = &ep_vtbl;
    w->alloc = alloc;
    w->ep = ep;
    w->doorbell = doorbell;
    atomic_init(&w->rung, false);
    return &w->base;
}

Mel_Vat_Waiter* mel_vat_waiter_ui(const Mel_Alloc* alloc) { return mel_vat_waiter_epoll(alloc); }

Mel_Vat_Waiter* mel_vat_waiter_io(const Mel_Alloc* alloc) { return mel_vat_waiter_epoll(alloc); }
