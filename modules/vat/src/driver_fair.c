#include "vat_internal.h"

#include <allocator/allocator.h>
#include <collection/list.h>
#include <debug/assert.h>

typedef struct
{
    Mel_Vat_Driver   base;
    const Mel_Alloc* alloc;
    u32              budget;
} Fair_Driver;

static bool fair_quitting(Mel_Vat* vat) { return atomic_load_explicit(&vat->quitting, memory_order_acquire); }

static bool fair_finish(Mel_Vat* vat, bool more)
{
    atomic_store_explicit(&vat->parked, true, memory_order_seq_cst);
    mel_vat__drain_mailbox(vat);
    if (vat->ready_head != NULL)
        vat->waiter->vt->ring(vat->waiter);
    return more;
}

static bool fair_turn(Mel_Vat_Driver* driver, Mel_Vat* vat)
{
    Fair_Driver* fair = mel_container_of(driver, Fair_Driver, base);

    atomic_store_explicit(&vat->parked, false, memory_order_relaxed);
    mel_vat__drain_mailbox(vat);
    usize ran = mel_vat__run_ready(vat, fair->budget);
    if (fair_quitting(vat))
        return fair_finish(vat, false);
    if (ran > 0)
        return fair_finish(vat, true);
    if (!mel_vat__retained(vat))
        return fair_finish(vat, false);

    i64 timeout = mel_vat__reduce(vat);
    atomic_store_explicit(&vat->parked, true, memory_order_seq_cst);
    mel_vat__drain_mailbox(vat);
    if (vat->ready_head != NULL)
        timeout = 0;
    vat->waiter->vt->wait(vat->waiter, timeout);
    atomic_store_explicit(&vat->parked, false, memory_order_relaxed);

    mel_vat__drain_mailbox(vat);
    mel_vat__drain_sources(vat, mel_vat__now(), fair->budget);
    mel_vat__run_ready(vat, fair->budget);

    if (fair_quitting(vat))
        return fair_finish(vat, false);
    return fair_finish(vat, mel_vat__retained(vat));
}

static void fair_close(Mel_Vat_Driver* driver)
{
    Fair_Driver* fair = mel_container_of(driver, Fair_Driver, base);
    mel_dealloc(fair->alloc, fair);
}

static const Mel_Vat_Driver_Vtbl fair_vtbl = { fair_turn, fair_close };

Mel_Vat_Driver* mel_vat_driver_fair(const Mel_Alloc* alloc, u32 budget)
{
    mel_assert(alloc != NULL);
    mel_assert(budget > 0);
    Fair_Driver* fair = mel_alloc_type(alloc, Fair_Driver);
    fair->base.vt = &fair_vtbl;
    fair->alloc = alloc;
    fair->budget = budget;
    return &fair->base;
}
