#include "vat_internal.h"

#include <allocator/allocator.h>
#include <collection/list.h>
#include <debug/assert.h>
#include <time/nano.h>

i64 mel_vat__now(void) { return (i64)mel_nanos_since_unspecified_epoch(); }

static void mel_vat__ready_push(Mel_Vat* vat, Mel_Mpsc_Node* node)
{
    atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
    if (vat->ready_tail != NULL)
        atomic_store_explicit(&vat->ready_tail->next, node, memory_order_relaxed);
    else
        vat->ready_head = node;
    vat->ready_tail = node;
}

void mel_vat__drain_mailbox(Mel_Vat* vat)
{
    for (;;)
    {
        Mel_Mpsc_Node* node = mel_mpsc_pop(&vat->mailbox);
        if (node == NULL)
            break;
        mel_vat__ready_push(vat, node);
    }
}

usize mel_vat__run_ready(Mel_Vat* vat, u32 budget)
{
    usize ran = 0;
    while (vat->ready_head != NULL && ran < budget)
    {
        Mel_Mpsc_Node* node = vat->ready_head;
        Mel_Task*      task = mel_container_of(node, Mel_Task, link);
        vat->ready_head = atomic_load_explicit(&node->next, memory_order_relaxed);
        if (vat->ready_head == NULL)
            vat->ready_tail = NULL;
        atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
        atomic_store_explicit(&task->armed, 0, memory_order_release);
        task->run(task);
        ran++;
    }
    return ran;
}

i64 mel_vat__reduce(Mel_Vat* vat)
{
    if (vat->ready_head != NULL)
        return 0;
    i64 min = MEL_VAT_NEVER;
    for (usize i = 0; i < vat->sources.count; i++)
    {
        Mel_Vat_Source* s = vat->sources.items[i];
        if (s->closing)
            continue;
        i64 d = s->vt->deadline != NULL ? s->vt->deadline(s) : MEL_VAT_NEVER;
        if (d < min)
            min = d;
    }
    if (min == MEL_VAT_NEVER)
        return -1;
    i64 now = mel_vat__now();
    i64 timeout = min - now;
    return timeout < 0 ? 0 : timeout;
}

static bool mel_vat__source_woke(Mel_Vat_Source* s)
{
    if (s->vt->wakeables == NULL)
        return false;
    Mel_Vat_Wakeable* set = NULL;
    usize             count = 0;
    s->vt->wakeables(s, &set, &count);
    bool woke = false;
    for (usize i = 0; i < count; i++)
    {
        if (set[i].revents != 0)
        {
            set[i].revents = 0;
            woke = true;
        }
    }
    return woke;
}

usize mel_vat__drain_sources(Mel_Vat* vat, i64 now, u32 budget)
{
    usize drained = 0;
    vat->draining++;
    usize count = vat->sources.count;
    for (usize i = 0; i < count; i++)
    {
        Mel_Vat_Source* s = vat->sources.items[i];
        if (s->closing)
            continue;
        bool due = s->vt->deadline != NULL && s->vt->deadline(s) <= now;
        bool woke = mel_vat__source_woke(s);
        if (!due && !woke)
            continue;
        if (s->vt->drain != NULL)
            s->vt->drain(s, budget);
        drained++;
    }
    vat->draining--;
    if (vat->draining == 0 && vat->reap_pending)
        mel_vat__reap(vat);
    return drained;
}

bool mel_vat__retained(const Mel_Vat* vat)
{
    if (vat->retains > 0)
        return true;
    if (vat->ready_head != NULL)
        return true;
    for (usize i = 0; i < vat->sources.count; i++)
    {
        if (!vat->sources.items[i]->closing)
            return true;
    }
    return false;
}

static void mel_vat__source_dispose(Mel_Vat* vat, Mel_Vat_Source* s)
{
    if (s->vt->wakeables != NULL)
    {
        Mel_Vat_Wakeable* set = NULL;
        usize             count = 0;
        s->vt->wakeables(s, &set, &count);
        for (usize i = 0; i < count; i++)
            vat->waiter->vt->disarm(vat->waiter, &set[i]);
    }
    if (s->vt->cancel != NULL)
        s->vt->cancel(s);
    mel_dealloc(vat->alloc, s);
}

void mel_vat__reap(Mel_Vat* vat)
{
    vat->reap_pending = false;
    usize i = 0;
    while (i < vat->sources.count)
    {
        Mel_Vat_Source* s = vat->sources.items[i];
        if (s->closing)
        {
            mel_array_remove_unordered(&vat->sources, i);
            mel_vat__source_dispose(vat, s);
            continue;
        }
        i++;
    }
}

static void mel_vat__executor_submit(Mel_Executor* self, Mel_Task* task)
{
    Mel_Vat* vat = mel_container_of(self, Mel_Vat, executor);
    mel_vat_post(vat, task);
}

Mel_Vat* mel_vat_open(const Mel_Alloc* alloc, Mel_Vat_Desc desc)
{
    mel_assert(alloc != NULL);
    mel_assert(desc.waiter != NULL);
    mel_assert(desc.driver != NULL);
    Mel_Vat* vat = mel_alloc_type(alloc, Mel_Vat);
    vat->alloc = alloc;
    vat->waiter = desc.waiter;
    vat->driver = desc.driver;
    vat->owner = mel_thread_current_id();
    mel_mpsc_init(&vat->mailbox);
    vat->ready_head = NULL;
    vat->ready_tail = NULL;
    mel_array_init(&vat->sources, alloc);
    vat->depth = 0;
    vat->draining = 0;
    vat->retains = 0;
    vat->reap_pending = false;
    atomic_init(&vat->quitting, false);
    atomic_init(&vat->parked, true);
    vat->executor.submit = mel_vat__executor_submit;
    return vat;
}

void mel_vat_close(Mel_Vat* vat)
{
    mel_assert(vat->depth == 0);
    for (usize i = 0; i < vat->sources.count; i++)
        mel_vat__source_dispose(vat, vat->sources.items[i]);
    mel_array_free(&vat->sources);
    mel_dealloc(vat->alloc, vat);
}

void mel_vat_retain(Mel_Vat* vat)
{
    mel_assert(mel_vat_is_owner(vat));
    vat->retains++;
}

void mel_vat_release(Mel_Vat* vat)
{
    mel_assert(mel_vat_is_owner(vat));
    mel_assert(vat->retains > 0);
    vat->retains--;
}

void mel_vat_run(Mel_Vat* vat)
{
    mel_assert(mel_vat_is_owner(vat));
    bool saved = atomic_load_explicit(&vat->quitting, memory_order_relaxed);
    atomic_store_explicit(&vat->quitting, false, memory_order_relaxed);
    vat->depth++;
    while (vat->driver->vt->turn(vat->driver, vat))
    {
    }
    vat->depth--;
    atomic_store_explicit(&vat->quitting, saved, memory_order_relaxed);
}

bool mel_vat_step(Mel_Vat* vat)
{
    mel_assert(mel_vat_is_owner(vat));
    vat->depth++;
    bool more = vat->driver->vt->turn(vat->driver, vat);
    vat->depth--;
    return more;
}

void mel_vat_quit(Mel_Vat* vat)
{
    atomic_store_explicit(&vat->quitting, true, memory_order_release);
    vat->waiter->vt->ring(vat->waiter);
}

i32 mel_vat_depth(const Mel_Vat* vat) { return vat->depth; }

void mel_vat_post(Mel_Vat* vat, Mel_Task* task)
{
    i32 expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&task->armed, &expected, 1, memory_order_acq_rel, memory_order_acquire))
        return;
    if (mel_vat_is_owner(vat))
    {
        mel_vat__ready_push(vat, &task->link);
        if (atomic_load_explicit(&vat->parked, memory_order_relaxed))
            vat->waiter->vt->ring(vat->waiter);
        return;
    }
    mel_mpsc_push(&vat->mailbox, &task->link);
    if (atomic_load_explicit(&vat->parked, memory_order_seq_cst))
        vat->waiter->vt->ring(vat->waiter);
}

Mel_Executor* mel_vat_executor(Mel_Vat* vat) { return &vat->executor; }

bool mel_vat_is_owner(const Mel_Vat* vat) { return mel_thread_id_equal(mel_thread_current_id(), vat->owner); }

const Mel_Alloc* mel_vat_alloc(const Mel_Vat* vat) { return vat->alloc; }

Mel_Vat_Waiter* mel_vat_waiter(Mel_Vat* vat) { return vat->waiter; }

Mel_Vat_Source* mel_vat_source_open(Mel_Vat* vat, const Mel_Vat_Source_Vtbl* vt, void* state)
{
    mel_assert(mel_vat_is_owner(vat));
    mel_assert(vt != NULL);
    Mel_Vat_Source* s = mel_alloc_type(vat->alloc, Mel_Vat_Source);
    s->vat = vat;
    s->vt = vt;
    s->state = state;
    s->closing = false;
    mel_array_push(&vat->sources, s);
    mel_vat_source_demand_changed(s);
    return s;
}

void mel_vat_source_close(Mel_Vat_Source* source)
{
    Mel_Vat* vat = source->vat;
    mel_assert(mel_vat_is_owner(vat));
    if (source->closing)
        return;
    source->closing = true;
    if (vat->draining > 0)
    {
        vat->reap_pending = true;
        return;
    }
    mel_vat__reap(vat);
}

void* mel_vat_source_state(Mel_Vat_Source* source) { return source->state; }

Mel_Vat* mel_vat_source_vat(Mel_Vat_Source* source) { return source->vat; }

void mel_vat_source_demand_changed(Mel_Vat_Source* source)
{
    Mel_Vat* vat = source->vat;
    if (!mel_vat_is_owner(vat))
    {
        vat->waiter->vt->ring(vat->waiter);
        return;
    }
    if (source->vt->wakeables != NULL)
    {
        Mel_Vat_Wakeable* set = NULL;
        usize             count = 0;
        source->vt->wakeables(source, &set, &count);
        for (usize i = 0; i < count; i++)
        {
            bool ok = vat->waiter->vt->arm(vat->waiter, &set[i]);
            mel_assert_msg("vat: waiter refused wakeable", ok);
            MEL_UNUSED(ok);
        }
    }
    if (atomic_load_explicit(&vat->parked, memory_order_relaxed))
        vat->waiter->vt->ring(vat->waiter);
}
