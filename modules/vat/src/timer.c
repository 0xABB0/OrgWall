#include "vat_internal.h"

#include <vat/timer.h>

#include <allocator/allocator.h>
#include <collection/heap.h>
#include <debug/assert.h>

typedef struct
{
    i64       deadline;
    u64       seq;
    Mel_Task* task;
} Timer_Entry;

#define TIMER_LT (a.deadline < b.deadline || (a.deadline == b.deadline && a.seq < b.seq))

struct Mel_Vat_Timers
{
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
    Mel_Vat_Source*  source;
    Mel_Heap(Timer_Entry) heap;
    u64 seq;
};

static i64 timers_deadline(Mel_Vat_Source* source)
{
    Mel_Vat_Timers* t = mel_vat_source_state(source);
    if (mel_heap_empty(&t->heap))
        return MEL_VAT_NEVER;
    return mel_heap_peek(&t->heap).deadline;
}

static bool timers_drain(Mel_Vat_Source* source, u32 budget)
{
    Mel_Vat_Timers* t = mel_vat_source_state(source);
    i64             now = mel_vat__now();
    u32             fired = 0;
    while (!mel_heap_empty(&t->heap) && fired < budget && mel_heap_peek(&t->heap).deadline <= now)
    {
        Timer_Entry due = mel_heap_pop(&t->heap, TIMER_LT);
        mel_vat_post(t->vat, due.task);
        fired++;
    }
    return !mel_heap_empty(&t->heap) && mel_heap_peek(&t->heap).deadline <= now;
}

static const Mel_Vat_Source_Vtbl timers_vtbl = {
    .wakeables = NULL,
    .deadline = timers_deadline,
    .drain = timers_drain,
    .cancel = NULL,
};

Mel_Vat_Timers* mel_vat_timers_open(Mel_Vat* vat, const Mel_Alloc* alloc)
{
    mel_assert(alloc != NULL);
    Mel_Vat_Timers* t = mel_alloc_type(alloc, Mel_Vat_Timers);
    t->vat = vat;
    t->alloc = alloc;
    mel_heap_init(&t->heap, alloc);
    t->seq = 0;
    t->source = mel_vat_source_open(vat, &timers_vtbl, t);
    return t;
}

void mel_vat_timers_close(Mel_Vat_Timers* timers)
{
    mel_vat_source_close(timers->source);
    mel_heap_free(&timers->heap);
    mel_dealloc(timers->alloc, timers);
}

void mel_vat_timers_add(Mel_Vat_Timers* timers, i64 deadline_ns, Mel_Task* task)
{
    mel_assert(mel_vat_is_owner(timers->vat));
    Timer_Entry entry = { deadline_ns, timers->seq++, task };
    mel_heap_push(&timers->heap, entry, TIMER_LT);
}

usize mel_vat_timers_pending(const Mel_Vat_Timers* timers) { return mel_heap_count(&timers->heap); }
