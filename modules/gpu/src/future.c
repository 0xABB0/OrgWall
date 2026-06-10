#include <gpu/future.h>

#include <gpu/status.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/list.h>
#include <executor/executor.h>
#include <thread/thread.h>
#include <thread/mutex.h>
#include <time/nano.h>
#include <vat/vat.h>
#include <debug/assert.h>

#include <stdatomic.h>

struct Mel_Gpu_Future
{
    Mel_Future           base;
    Mel_Task             cont_task;
    Mel_Gpu_Continuation cont;
    void*                cont_user;
    Mel_Executor*        target_exec;
    Mel_Vat*             target_vat;
    _Atomic(i32)         status_claimed;
    _Atomic(u32)         gpu_status;
};

typedef struct
{
    Mel_Gpu_Poll_Fn fn;
    void*           user;
} Mel_Gpu_Poller;

struct Mel_Gpu_Completion_Pump
{
    Mel_Vat*        vat;
    Mel_Vat_Source* source;
    i64             interval;
    i64             next;
    Mel_Mutex       lock;
    Mel_Gpu_Poller* pollers;
    _Atomic(u32)    poller_count;
    u32             poller_capacity;
};

static i64 mel_gpu__pump_deadline(Mel_Vat_Source* source)
{
    Mel_Gpu_Completion_Pump* pump = mel_vat_source_state(source);
    if (atomic_load_explicit(&pump->poller_count, memory_order_acquire) == 0)
        return MEL_VAT_NEVER;
    return pump->next;
}

static bool mel_gpu__pump_drain(Mel_Vat_Source* source, u32 budget)
{
    MEL_UNUSED(budget);
    Mel_Gpu_Completion_Pump* pump = mel_vat_source_state(source);
    pump->next = (i64)mel_nanos_since_unspecified_epoch() + pump->interval;
    mel_gpu_pump_tick(pump);
    return false;
}

static const Mel_Vat_Source_Vtbl mel_gpu__pump_vtbl = {
    .wakeables = NULL,
    .deadline = mel_gpu__pump_deadline,
    .drain = mel_gpu__pump_drain,
    .cancel = NULL,
};

Mel_Gpu_Completion_Pump* mel_gpu_pump_create_opt(Mel_Vat* vat, Mel_Gpu_Pump_Opt opt)
{
    const Mel_Alloc*         a = mel_alloc_heap();
    Mel_Gpu_Completion_Pump* pump = mel_alloc_type(a, Mel_Gpu_Completion_Pump);
    *pump = (Mel_Gpu_Completion_Pump){ 0 };
    pump->vat = vat;
    pump->interval = opt.tick_interval_ns > 0 ? opt.tick_interval_ns : 2000000;
    mel_mutex_init(&pump->lock, MEL_MUTEX_PLAIN);

    if (vat)
        pump->source = mel_vat_source_open(vat, &mel_gpu__pump_vtbl, pump);
    return pump;
}

void mel_gpu_pump_destroy(Mel_Gpu_Completion_Pump* pump)
{
    if (!pump)
        return;
    if (pump->source)
        mel_vat_source_close(pump->source);
    mel_mutex_destroy(&pump->lock);
    const Mel_Alloc* a = mel_alloc_heap();
    if (pump->pollers)
        mel_dealloc(a, pump->pollers);
    mel_dealloc(a, pump);
}

Mel_Vat* mel_gpu_pump_vat(Mel_Gpu_Completion_Pump* pump) { return pump ? pump->vat : NULL; }

void mel_gpu_pump_add_poller(Mel_Gpu_Completion_Pump* pump, Mel_Gpu_Poll_Fn fn, void* user)
{
    mel_mutex_lock(&pump->lock);
    u32 count = atomic_load_explicit(&pump->poller_count, memory_order_relaxed);
    if (count == pump->poller_capacity)
    {
        u32              cap = pump->poller_capacity ? pump->poller_capacity * 2 : 8;
        const Mel_Alloc* a = mel_alloc_heap();
        pump->pollers = pump->pollers ? mel_realloc(a, pump->pollers, sizeof(Mel_Gpu_Poller) * cap) : mel_alloc(a, sizeof(Mel_Gpu_Poller) * cap);
        pump->poller_capacity = cap;
    }
    pump->pollers[count] = (Mel_Gpu_Poller){ .fn = fn, .user = user };
    atomic_store_explicit(&pump->poller_count, count + 1, memory_order_release);
    mel_mutex_unlock(&pump->lock);
    if (count == 0 && pump->source)
        mel_vat_source_demand_changed(pump->source);
}

void mel_gpu_pump_remove_poller(Mel_Gpu_Completion_Pump* pump, Mel_Gpu_Poll_Fn fn, void* user)
{
    mel_mutex_lock(&pump->lock);
    u32 count = atomic_load_explicit(&pump->poller_count, memory_order_relaxed);
    for (u32 i = 0; i < count; i++)
    {
        if (pump->pollers[i].fn == fn && pump->pollers[i].user == user)
        {
            count--;
            pump->pollers[i] = pump->pollers[count];
            atomic_store_explicit(&pump->poller_count, count, memory_order_release);
            break;
        }
    }
    mel_mutex_unlock(&pump->lock);
    if (count == 0 && pump->source)
        mel_vat_source_demand_changed(pump->source);
}

void mel_gpu_pump_tick(Mel_Gpu_Completion_Pump* pump)
{
    if (!pump)
        return;

    mel_mutex_lock(&pump->lock);
    u32             n = atomic_load_explicit(&pump->poller_count, memory_order_relaxed);
    Mel_Gpu_Poller* snapshot = NULL;
    if (n)
    {
        snapshot = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Poller, n);
        for (u32 i = 0; i < n; i++)
            snapshot[i] = pump->pollers[i];
    }
    mel_mutex_unlock(&pump->lock);

    for (u32 i = 0; i < n; i++)
        snapshot[i].fn(snapshot[i].user);
    if (snapshot)
        mel_dealloc(mel_alloc_heap(), snapshot);
}

static Mel_Executor* mel_gpu__target_executor(Mel_Vat* target) { return target ? mel_vat_executor(target) : mel_executor_inline(); }

static void mel_gpu__future_cont_run(Mel_Task* self)
{
    Mel_Gpu_Future* f = mel_container_of(self, Mel_Gpu_Future, cont_task);
    if (f->cont)
        f->cont(f, f->cont_user);
}

Mel_Gpu_Future* mel_gpu_future_create(Mel_Gpu_Completion_Pump* pump, Mel_Vat* target)
{
    (void)pump;
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Gpu_Future*  f = mel_alloc_type(a, Mel_Gpu_Future);
    *f = (Mel_Gpu_Future){ 0 };
    mel_future_init(&f->base, NULL, a);
    f->target_vat = target;
    f->target_exec = mel_gpu__target_executor(target);
    atomic_store_explicit(&f->gpu_status, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK), memory_order_relaxed);
    return f;
}

void mel_gpu_future_destroy(Mel_Gpu_Future* f)
{
    if (!f)
        return;
    mel_dealloc(mel_alloc_heap(), f);
}

void mel_gpu_future_then(Mel_Gpu_Future* f, Mel_Gpu_Continuation cont, void* user)
{
    mel_assert(f != NULL);
    f->cont = cont;
    f->cont_user = user;
    mel_task_init(&f->cont_task, mel_gpu__future_cont_run);
    mel_future_then(&f->base, &f->cont_task, f->target_exec);
}

void mel_gpu_future_resolve(Mel_Gpu_Future* f, void* value, u32 status)
{
    mel_assert(f != NULL);
    if (atomic_exchange_explicit(&f->status_claimed, 1, memory_order_acq_rel) != 0)
        return;
    atomic_store_explicit(&f->gpu_status, status, memory_order_release);
    mel_future_resolve(&f->base, value, (Mel_Future_Status)mel_gpu_severity(status));
}

bool mel_gpu_future_resolved(Mel_Gpu_Future* f)
{
    mel_assert(f != NULL);
    return mel_future_resolved(&f->base);
}

void* mel_gpu_future_value(Mel_Gpu_Future* f)
{
    mel_assert(f != NULL);
    return mel_future_resolved(&f->base) ? mel_future_value(&f->base) : NULL;
}

u32 mel_gpu_future_status(Mel_Gpu_Future* f)
{
    mel_assert(f != NULL);
    return atomic_load_explicit(&f->gpu_status, memory_order_acquire);
}

u32 mel_gpu_future_wait(Mel_Gpu_Future* f)
{
    mel_assert(f != NULL);
    mel_assert(!f->target_vat || !mel_vat_is_owner(f->target_vat));
    while (!mel_future_resolved(&f->base))
        mel_thread_sleep(100000);
    return atomic_load_explicit(&f->gpu_status, memory_order_acquire);
}

Mel_Future* mel_gpu_future_shared(Mel_Gpu_Future* f) { return f ? &f->base : NULL; }
