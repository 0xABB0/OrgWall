#include <gpu/future.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <thread/thread.h>
#include <thread/mutex.h>
#include <log/log.h>
#include <debug/assert.h>

#include <stdatomic.h>

struct Mel_Gpu_Future
{
    _Atomic(i32)             claimed;
    _Atomic(i32)             resolved;
    void*                    value;
    u32                      status;
    Mel_Reactor*             target;
    Mel_Gpu_Continuation     cont;
    void*                    cont_user;
    Mel_Gpu_Completion_Pump* pump;
    Mel_Gpu_Future*          ready_next;
    bool                     delivered;
};

typedef struct
{
    Mel_Gpu_Poll_Fn fn;
    void*           user;
} Mel_Gpu_Poller;

struct Mel_Gpu_Completion_Pump
{
    Mel_Reactor*        reactor;
    Mel_Reactor_Source* timer;
    Mel_Mutex           lock;
    Mel_Gpu_Future*     ready_head;
    Mel_Gpu_Future*     ready_tail;
    _Atomic(i32)        ready_count;
    _Atomic(i32)        drain_posted;
    Mel_Gpu_Poller*     pollers;
    u32                 poller_count;
    u32                 poller_capacity;
    u32                 high_water;
    bool                warned;
};

static bool mel_gpu__pump_timer_cb(void* user)
{
    mel_gpu_pump_tick((Mel_Gpu_Completion_Pump*)user);
    return true;
}

Mel_Gpu_Completion_Pump* mel_gpu_pump_create_opt(Mel_Reactor* reactor, Mel_Gpu_Pump_Opt opt)
{
    const Mel_Alloc*         a = mel_alloc_heap();
    Mel_Gpu_Completion_Pump* pump = mel_alloc_type(a, Mel_Gpu_Completion_Pump);
    *pump = (Mel_Gpu_Completion_Pump){ 0 };
    pump->reactor = reactor;
    pump->high_water = opt.high_water ? opt.high_water : 256;
    mel_mutex_init(&pump->lock, MEL_MUTEX_PLAIN);

    if (reactor)
    {
        i64 interval = opt.tick_interval_ns > 0 ? opt.tick_interval_ns : 2000000;
        pump->timer = mel_reactor_timer_new(interval, mel_gpu__pump_timer_cb, pump);
        mel_reactor_source_attach(reactor, pump->timer);
    }
    return pump;
}

void mel_gpu_pump_destroy(Mel_Gpu_Completion_Pump* pump)
{
    if (!pump)
        return;
    if (pump->timer)
        mel_reactor_source_destroy(pump->timer);
    mel_mutex_destroy(&pump->lock);
    const Mel_Alloc* a = mel_alloc_heap();
    if (pump->pollers)
        mel_dealloc(a, pump->pollers);
    mel_dealloc(a, pump);
}

Mel_Reactor* mel_gpu_pump_reactor(Mel_Gpu_Completion_Pump* pump) { return pump ? pump->reactor : NULL; }

void mel_gpu_pump_add_poller(Mel_Gpu_Completion_Pump* pump, Mel_Gpu_Poll_Fn fn, void* user)
{
    mel_mutex_lock(&pump->lock);
    if (pump->poller_count == pump->poller_capacity)
    {
        u32              cap = pump->poller_capacity ? pump->poller_capacity * 2 : 8;
        const Mel_Alloc* a = mel_alloc_heap();
        pump->pollers = pump->pollers ? mel_realloc(a, pump->pollers, sizeof(Mel_Gpu_Poller) * cap) : mel_alloc(a, sizeof(Mel_Gpu_Poller) * cap);
        pump->poller_capacity = cap;
    }
    pump->pollers[pump->poller_count++] = (Mel_Gpu_Poller){ .fn = fn, .user = user };
    mel_mutex_unlock(&pump->lock);
}

void mel_gpu_pump_remove_poller(Mel_Gpu_Completion_Pump* pump, Mel_Gpu_Poll_Fn fn, void* user)
{
    mel_mutex_lock(&pump->lock);
    for (u32 i = 0; i < pump->poller_count; i++)
    {
        if (pump->pollers[i].fn == fn && pump->pollers[i].user == user)
        {
            pump->pollers[i] = pump->pollers[--pump->poller_count];
            break;
        }
    }
    mel_mutex_unlock(&pump->lock);
}

void mel_gpu_pump_tick(Mel_Gpu_Completion_Pump* pump)
{
    if (!pump)
        return;

    atomic_store(&pump->drain_posted, 0);

    mel_mutex_lock(&pump->lock);
    u32             n = pump->poller_count;
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

    mel_mutex_lock(&pump->lock);
    Mel_Gpu_Future* head = pump->ready_head;
    pump->ready_head = NULL;
    pump->ready_tail = NULL;
    mel_mutex_unlock(&pump->lock);

    while (head)
    {
        Mel_Gpu_Future* f = head;
        head = f->ready_next;
        f->ready_next = NULL;
        atomic_fetch_sub(&pump->ready_count, 1);
        f->delivered = true;
        if (f->cont)
            f->cont(f, f->cont_user);
    }

    if (pump->warned && (u32)atomic_load(&pump->ready_count) <= pump->high_water)
        pump->warned = false;
}

Mel_Gpu_Future* mel_gpu_future_create(Mel_Gpu_Completion_Pump* pump, Mel_Reactor* target)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Gpu_Future*  f = mel_alloc_type(a, Mel_Gpu_Future);
    *f = (Mel_Gpu_Future){ 0 };
    f->pump = pump;
    f->target = target;
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
    f->cont = cont;
    f->cont_user = user;
}

static void mel_gpu__pump_enqueue(Mel_Gpu_Completion_Pump* pump, Mel_Gpu_Future* f)
{
    mel_mutex_lock(&pump->lock);
    f->ready_next = NULL;
    if (pump->ready_tail)
        pump->ready_tail->ready_next = f;
    else
        pump->ready_head = f;
    pump->ready_tail = f;
    mel_mutex_unlock(&pump->lock);

    i32 depth = atomic_fetch_add(&pump->ready_count, 1) + 1;
    if ((u32)depth > pump->high_water && !pump->warned)
    {
        pump->warned = true;
        mel_log_warn("gpu", "completion pump backpressure: %d in-flight completions exceed high-water %u", depth, pump->high_water);
    }
    mel_assert((u32)depth <= pump->high_water * 4u);

    if (pump->reactor && !mel_reactor_is_owner(pump->reactor))
    {
        if (atomic_exchange(&pump->drain_posted, 1) == 0)
            mel_reactor_post(pump->reactor, (Mel_Reactor_Post_Proc)mel_gpu_pump_tick, pump);
    }
}

void mel_gpu_future_resolve(Mel_Gpu_Future* f, void* value, u32 status)
{
    if (atomic_exchange(&f->claimed, 1) != 0)
        return;

    f->value = value;
    f->status = status;
    atomic_store_explicit(&f->resolved, 1, memory_order_release);

    if (!f->pump)
    {
        f->delivered = true;
        if (f->cont)
            f->cont(f, f->cont_user);
        return;
    }

    mel_gpu__pump_enqueue(f->pump, f);
}

bool mel_gpu_future_resolved(Mel_Gpu_Future* f) { return atomic_load_explicit(&f->resolved, memory_order_acquire) != 0; }

void* mel_gpu_future_value(Mel_Gpu_Future* f) { return f->value; }

u32 mel_gpu_future_status(Mel_Gpu_Future* f) { return f->status; }

u32 mel_gpu_future_wait(Mel_Gpu_Future* f)
{
    mel_assert(!f->pump || !f->pump->reactor || !mel_reactor_is_owner(f->pump->reactor));
    while (atomic_load(&f->resolved) == 0)
        mel_thread_sleep(100000);
    return f->status;
}
