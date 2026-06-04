#include <executor/executor.h>
#include <test/test.h>

#include <allocator/heap.h>
#include <allocator/allocator.h>
#include <collection/list.h>

#include <stdatomic.h>

typedef struct
{
    Mel_Task task;
    int      ran;
    int      tag;
} Probe;

static void probe_run(Mel_Task* self)
{
    Probe* p = mel_container_of(self, Probe, task);
    p->ran++;
}

MEL_TEST(executor, inline_runs_plain_function)
{
    Mel_Executor* exec = mel_executor_inline();
    Probe         p = { 0 };
    mel_task_init(&p.task, probe_run);

    mel_executor_submit(exec, &p.task);

    MEL_EXPECT_EQ(p.ran, 1);
}

MEL_TEST(executor, container_of_recovers_owner)
{
    Mel_Executor* exec = mel_executor_inline();
    Probe         p = { 0 };
    p.tag = 0xABCD;
    mel_task_init(&p.task, probe_run);

    mel_executor_submit(exec, &p.task);

    MEL_EXPECT_EQ(p.ran, 1);
    MEL_EXPECT_EQ(p.tag, 0xABCD);
}

static int g_order_count;
static int g_order_a;
static int g_order_b;
static int g_order_c;

typedef struct
{
    Mel_Task task;
    int      id;
} Ordered;

static void ordered_run(Mel_Task* self)
{
    Ordered* o = mel_container_of(self, Ordered, task);
    int      slot = g_order_count++;
    if (slot == 0)
        g_order_a = o->id;
    else if (slot == 1)
        g_order_b = o->id;
    else if (slot == 2)
        g_order_c = o->id;
}

typedef struct
{
    Mel_Task      task;
    Mel_Executor* exec;
    Ordered*      a;
    Ordered*      b;
    Ordered*      c;
} Ordered_Driver;

static void ordered_driver_run(Mel_Task* self)
{
    Ordered_Driver* d = mel_container_of(self, Ordered_Driver, task);
    mel_executor_submit(d->exec, &d->a->task);
    mel_executor_submit(d->exec, &d->b->task);
    mel_executor_submit(d->exec, &d->c->task);
}

MEL_TEST(executor, inline_fifo_within_drain)
{
    Mel_Executor* exec = mel_executor_inline();

    Ordered a = { 0 }, b = { 0 }, c = { 0 };
    a.id = 1;
    b.id = 2;
    c.id = 3;
    mel_task_init(&a.task, ordered_run);
    mel_task_init(&b.task, ordered_run);
    mel_task_init(&c.task, ordered_run);

    Ordered_Driver d = { 0 };
    d.exec = exec;
    d.a = &a;
    d.b = &b;
    d.c = &c;
    mel_task_init(&d.task, ordered_driver_run);

    mel_executor_submit(exec, &d.task);

    MEL_REQUIRE_EQ(g_order_count, 3);
    MEL_EXPECT_EQ(g_order_a, 1);
    MEL_EXPECT_EQ(g_order_b, 2);
    MEL_EXPECT_EQ(g_order_c, 3);
}

typedef struct
{
    Mel_Task      task;
    Mel_Executor* exec;
    int           in_flight;
    int           max_in_flight;
    int           iterations;
    char*         first_frame;
    isize         max_stack_delta;
} Recurser;

static void recurser_run(Mel_Task* self)
{
    Recurser* r = mel_container_of(self, Recurser, task);

    char  local;
    char* frame = &local;
    if (r->first_frame == NULL)
        r->first_frame = frame;
    isize delta = frame - r->first_frame;
    if (delta < 0)
        delta = -delta;
    if (delta > r->max_stack_delta)
        r->max_stack_delta = delta;

    r->in_flight++;
    if (r->in_flight > r->max_in_flight)
        r->max_in_flight = r->in_flight;

    r->iterations++;
    if (r->iterations < 1000)
        mel_executor_submit(r->exec, &r->task);

    r->in_flight--;
}

MEL_TEST(executor, inline_trampoline_prevents_recursion)
{
    Mel_Executor* exec = mel_executor_inline();
    Recurser      r = { 0 };
    r.exec = exec;
    mel_task_init(&r.task, recurser_run);

    mel_executor_submit(exec, &r.task);

    MEL_EXPECT_EQ(r.iterations, 1000);
    MEL_EXPECT_EQ(r.max_in_flight, 1);
    MEL_EXPECT_LT(r.max_stack_delta, (isize)4096);
}

typedef struct
{
    Mel_Task task;
    int      ran;
} Coalesce;

static void coalesce_run(Mel_Task* self)
{
    Coalesce* c = mel_container_of(self, Coalesce, task);
    c->ran++;
}

MEL_TEST(executor, armed_coalesces_two_submits_before_run)
{
    Coalesce c = { 0 };
    mel_task_init(&c.task, coalesce_run);

    i32  e1 = 0;
    bool first = atomic_compare_exchange_strong(&c.task.armed, &e1, 1);
    i32  e2 = 0;
    bool second = atomic_compare_exchange_strong(&c.task.armed, &e2, 1);

    MEL_EXPECT(first);
    MEL_EXPECT(!second);
}

typedef struct
{
    Mel_Task task;
    int      ran;
} Waketask;

static void waketask_run(Mel_Task* self)
{
    Waketask* w = mel_container_of(self, Waketask, task);
    w->ran++;
}

MEL_TEST(executor, resubmit_waker_lands_the_task)
{
    Mel_Executor* exec = mel_executor_inline();
    Waketask      w = { 0 };
    mel_task_init(&w.task, waketask_run);

    Mel_Resubmit_Cell cell = { .exec = exec, .task = &w.task };
    Mel_Waker         waker = mel_resubmit_waker(&cell);

    waker.wake(waker.user);

    MEL_EXPECT_EQ(w.ran, 1);
}

MEL_TEST(executor, resubmit_waker_coalesces_two_wakes_before_run)
{
    Coalesce c = { 0 };
    mel_task_init(&c.task, coalesce_run);

    i32  e = 0;
    bool armed = atomic_compare_exchange_strong(&c.task.armed, &e, 1);
    MEL_REQUIRE(armed);

    Mel_Executor*     exec = mel_executor_inline();
    Mel_Resubmit_Cell cell = { .exec = exec, .task = &c.task };
    Mel_Waker         waker = mel_resubmit_waker(&cell);

    waker.wake(waker.user);
    waker.wake(waker.user);

    MEL_EXPECT_EQ(c.ran, 0);
}

typedef struct
{
    Mel_Task      task;
    Mel_Executor* exec;
    int*          ran;
    int           rearmed;
} Rearm;

static void rearm_run(Mel_Task* self)
{
    Rearm* r = mel_container_of(self, Rearm, task);
    (*r->ran)++;
    if (r->rearmed == 0)
    {
        r->rearmed = 1;
        mel_executor_submit(r->exec, &r->task);
    }
}

MEL_TEST(executor, resubmit_from_within_run_rearms)
{
    Mel_Executor* exec = mel_executor_inline();
    int           ran = 0;
    Rearm         r = { 0 };
    r.exec = exec;
    r.ran = &ran;
    mel_task_init(&r.task, rearm_run);

    mel_executor_submit(exec, &r.task);

    MEL_EXPECT_EQ(ran, 2);
}

static _Atomic(i64) g_live_allocs;

static void* counting_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user_data)
{
    const Mel_Alloc* heap = mel_alloc_heap();

    if (ptr == NULL)
    {
        void* p = heap->alloc_cb(NULL, size, align, file, func, line, heap->user_data);
        if (p)
            atomic_fetch_add_explicit(&g_live_allocs, 1, memory_order_relaxed);
        return p;
    }

    if (size == 0)
    {
        heap->alloc_cb(ptr, 0, align, file, func, line, heap->user_data);
        atomic_fetch_sub_explicit(&g_live_allocs, 1, memory_order_relaxed);
        return NULL;
    }

    return heap->alloc_cb(ptr, size, align, file, func, line, heap->user_data);
}

static int g_call_ran;
static i64 g_live_during_run;

static void call_fn(void* data)
{
    int* p = (int*)data;
    *p += 41;
    g_call_ran++;
    g_live_during_run = (i64)atomic_load(&g_live_allocs);
}

MEL_TEST(executor, call_runs_then_frees_no_leak)
{
    atomic_store(&g_live_allocs, 0);

    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Executor* exec = mel_executor_inline();
    int           value = 1;

    mel_executor_call(exec, call_fn, &value, &counting);

    MEL_EXPECT_EQ(g_call_ran, 1);
    MEL_EXPECT_EQ(value, 42);
    MEL_EXPECT_EQ((i64)atomic_load(&g_live_allocs), (i64)0);
}

MEL_TEST(executor, call_allocates_exactly_one_node)
{
    atomic_store(&g_live_allocs, 0);
    g_live_during_run = -1;

    Mel_Alloc     counting = { .alloc_cb = counting_cb, .user_data = NULL };
    Mel_Executor* exec = mel_executor_inline();
    int           value = 0;

    mel_executor_call(exec, call_fn, &value, &counting);

    MEL_EXPECT_EQ(g_live_during_run, (i64)1);
    MEL_EXPECT_EQ((i64)atomic_load(&g_live_allocs), (i64)0);
}

typedef struct
{
    Mel_Task      task;
    Mel_Executor* exec;
    isize         depth;
    isize         target;
    int           max_in_flight;
    int           in_flight;
    char*         first_frame;
    isize         max_stack_delta;
} Deep;

static void deep_run(Mel_Task* self)
{
    Deep* d = mel_container_of(self, Deep, task);

    char  local;
    char* frame = &local;
    if (d->first_frame == NULL)
        d->first_frame = frame;
    isize delta = frame - d->first_frame;
    if (delta < 0)
        delta = -delta;
    if (delta > d->max_stack_delta)
        d->max_stack_delta = delta;

    d->in_flight++;
    if (d->in_flight > d->max_in_flight)
        d->max_in_flight = d->in_flight;

    d->depth++;
    if (d->depth < d->target)
        mel_executor_submit(d->exec, &d->task);

    d->in_flight--;
}

MEL_TEST(executor, deep_self_resubmit_is_constant_stack)
{
    Mel_Executor* exec = mel_executor_inline();
    Deep          d = { 0 };
    d.exec = exec;
    d.target = 1000000;
    mel_task_init(&d.task, deep_run);

    mel_executor_submit(exec, &d.task);

    MEL_EXPECT_EQ(d.depth, (isize)1000000);
    MEL_EXPECT_EQ(d.max_in_flight, 1);
    MEL_EXPECT_LT(d.max_stack_delta, (isize)4096);
}

static _Atomic(i64) g_interleave_seq;

typedef struct
{
    Mel_Task task;
    int      id;
    i64      first_seq;
    int      runs;
} Inter;

static void inter_run(Mel_Task* self)
{
    Inter* it = mel_container_of(self, Inter, task);
    if (it->runs == 0)
        it->first_seq = atomic_fetch_add_explicit(&g_interleave_seq, 1, memory_order_relaxed);
    it->runs++;
}

typedef struct
{
    Mel_Task      task;
    Mel_Executor* exec;
    Inter*        x;
    Inter*        y;
    Inter*        z;
} Inter_Driver;

static void inter_driver_run(Mel_Task* self)
{
    Inter_Driver* d = mel_container_of(self, Inter_Driver, task);
    mel_executor_submit(d->exec, &d->x->task);
    mel_executor_submit(d->exec, &d->y->task);
    mel_executor_submit(d->exec, &d->x->task);
    mel_executor_submit(d->exec, &d->z->task);
    mel_executor_submit(d->exec, &d->y->task);
}

MEL_TEST(executor, interleaved_mid_drain_is_fifo_and_coalesced)
{
    Mel_Executor* exec = mel_executor_inline();
    atomic_store(&g_interleave_seq, 0);

    Inter x = { 0 }, y = { 0 }, z = { 0 };
    x.id = 1;
    y.id = 2;
    z.id = 3;
    mel_task_init(&x.task, inter_run);
    mel_task_init(&y.task, inter_run);
    mel_task_init(&z.task, inter_run);

    Inter_Driver d = { 0 };
    d.exec = exec;
    d.x = &x;
    d.y = &y;
    d.z = &z;
    mel_task_init(&d.task, inter_driver_run);

    mel_executor_submit(exec, &d.task);

    MEL_EXPECT_EQ(x.runs, 1);
    MEL_EXPECT_EQ(y.runs, 1);
    MEL_EXPECT_EQ(z.runs, 1);
    MEL_EXPECT_EQ(x.first_seq, (i64)0);
    MEL_EXPECT_EQ(y.first_seq, (i64)1);
    MEL_EXPECT_EQ(z.first_seq, (i64)2);
}

MEL_TEST(executor, waker_fired_many_times_before_run_runs_once)
{
    Mel_Executor* exec = mel_executor_inline();
    Coalesce      c = { 0 };
    mel_task_init(&c.task, coalesce_run);

    i32  e = 0;
    bool armed = atomic_compare_exchange_strong(&c.task.armed, &e, 1);
    MEL_REQUIRE(armed);

    Mel_Resubmit_Cell cell = { .exec = exec, .task = &c.task };
    Mel_Waker         waker = mel_resubmit_waker(&cell);

    for (int i = 0; i < 1000; i++)
        waker.wake(waker.user);

    MEL_EXPECT_EQ(c.ran, 0);

    atomic_store_explicit(&c.task.armed, 0, memory_order_release);
    mel_executor_submit(exec, &c.task);
    MEL_EXPECT_EQ(c.ran, 1);
}

MEL_TEST(executor, waker_fired_after_run_completes_runs_again)
{
    Mel_Executor* exec = mel_executor_inline();
    Coalesce      c = { 0 };
    mel_task_init(&c.task, coalesce_run);

    Mel_Resubmit_Cell cell = { .exec = exec, .task = &c.task };
    Mel_Waker         waker = mel_resubmit_waker(&cell);

    waker.wake(waker.user);
    MEL_EXPECT_EQ(c.ran, 1);

    waker.wake(waker.user);
    MEL_EXPECT_EQ(c.ran, 2);

    waker.wake(waker.user);
    MEL_EXPECT_EQ(c.ran, 3);
}

typedef struct
{
    Mel_Task task;
    int      ran;
} Inner;

static void inner_run(Mel_Task* self)
{
    Inner* in = mel_container_of(self, Inner, task);
    in->ran++;
}

typedef struct
{
    Mel_Task      task;
    Mel_Executor* exec;
    Inner*        inner;
    int           inner_ran_seen_mid_run;
    int           reentered;
} Reentry;

static void reentry_run(Mel_Task* self)
{
    Reentry* r = mel_container_of(self, Reentry, task);
    r->reentered = 1;
    mel_executor_submit(r->exec, &r->inner->task);
    r->inner_ran_seen_mid_run = r->inner->ran;
}

MEL_TEST(executor, submit_from_inside_run_enqueues_not_recurses)
{
    Mel_Executor* exec = mel_executor_inline();

    Inner inner = { 0 };
    mel_task_init(&inner.task, inner_run);

    Reentry r = { 0 };
    r.exec = exec;
    r.inner = &inner;
    mel_task_init(&r.task, reentry_run);

    mel_executor_submit(exec, &r.task);

    MEL_EXPECT_EQ(r.reentered, 1);
    MEL_EXPECT_EQ(r.inner_ran_seen_mid_run, 0);
    MEL_EXPECT_EQ(inner.ran, 1);
}

static _Atomic(i64) g_oc_outstanding;
static _Atomic(i64) g_oc_total_allocs;
static _Atomic(i64) g_oc_total_frees;

static void* overcount_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user_data)
{
    const Mel_Alloc* heap = mel_alloc_heap();

    if (ptr == NULL)
    {
        void* p = heap->alloc_cb(NULL, size, align, file, func, line, heap->user_data);
        if (p)
        {
            atomic_fetch_add_explicit(&g_oc_outstanding, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&g_oc_total_allocs, 2, memory_order_relaxed);
        }
        return p;
    }

    if (size == 0)
    {
        heap->alloc_cb(ptr, 0, align, file, func, line, heap->user_data);
        atomic_fetch_sub_explicit(&g_oc_outstanding, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_oc_total_frees, 1, memory_order_relaxed);
        return NULL;
    }

    return heap->alloc_cb(ptr, size, align, file, func, line, heap->user_data);
}

static int g_oc_ran;

static void oc_fn(void* data)
{
    (void)data;
    g_oc_ran++;
}

MEL_TEST(executor, call_under_overcounting_allocator_no_leak_no_double_free)
{
    atomic_store(&g_oc_outstanding, 0);
    atomic_store(&g_oc_total_allocs, 0);
    atomic_store(&g_oc_total_frees, 0);
    g_oc_ran = 0;

    Mel_Alloc     oc = { .alloc_cb = overcount_cb, .user_data = NULL };
    Mel_Executor* exec = mel_executor_inline();
    int           value = 0;

    mel_executor_call(exec, oc_fn, &value, &oc);

    MEL_EXPECT_EQ(g_oc_ran, 1);
    MEL_EXPECT_EQ((i64)atomic_load(&g_oc_outstanding), (i64)0);
    MEL_EXPECT_EQ((i64)atomic_load(&g_oc_total_frees), (i64)1);
}
