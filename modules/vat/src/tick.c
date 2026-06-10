#include "vat_internal.h"

#include <vat/tick.h>

#include <allocator/allocator.h>
#include <debug/assert.h>

struct Mel_Vat_Tick
{
    const Mel_Alloc* alloc;
    Mel_Vat_Source*  source;
    Mel_Vat_Tick_Fn  fn;
    void*            user;
    i64              interval;
    i64              next;
    bool             stopped;
};

static i64 tick_deadline(Mel_Vat_Source* source)
{
    Mel_Vat_Tick* t = mel_vat_source_state(source);
    return t->stopped ? MEL_VAT_NEVER : t->next;
}

static bool tick_drain(Mel_Vat_Source* source, u32 budget)
{
    (void)budget;
    Mel_Vat_Tick* t = mel_vat_source_state(source);
    if (t->stopped)
        return false;
    t->next += t->interval;
    i64 now = mel_vat__now();
    if (t->next <= now)
        t->next = now + t->interval;
    if (!t->fn(t->user))
        t->stopped = true;
    return false;
}

static const Mel_Vat_Source_Vtbl tick_vtbl = {
    .wakeables = NULL,
    .deadline = tick_deadline,
    .drain = tick_drain,
    .cancel = NULL,
};

Mel_Vat_Tick* mel_vat_tick_open(Mel_Vat* vat, const Mel_Alloc* alloc, i64 interval_ns, Mel_Vat_Tick_Fn fn, void* user)
{
    mel_assert(alloc != NULL);
    mel_assert(interval_ns > 0);
    mel_assert(fn != NULL);
    Mel_Vat_Tick* t = mel_alloc_type(alloc, Mel_Vat_Tick);
    t->alloc = alloc;
    t->fn = fn;
    t->user = user;
    t->interval = interval_ns;
    t->next = mel_vat__now() + interval_ns;
    t->stopped = false;
    t->source = mel_vat_source_open(vat, &tick_vtbl, t);
    return t;
}

void mel_vat_tick_close(Mel_Vat_Tick* tick)
{
    mel_vat_source_close(tick->source);
    mel_dealloc(tick->alloc, tick);
}

void mel_vat_tick_set_interval(Mel_Vat_Tick* tick, i64 interval_ns)
{
    mel_assert(interval_ns > 0);
    tick->interval = interval_ns;
    tick->next = mel_vat__now() + interval_ns;
    tick->stopped = false;
    mel_vat_source_demand_changed(tick->source);
}

void mel_vat_tick_pause(Mel_Vat_Tick* tick)
{
    tick->stopped = true;
    mel_vat_source_demand_changed(tick->source);
}
