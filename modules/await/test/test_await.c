#include <await/await.h>

#include <test/test.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <channel/channel.h>
#include <collection/list.h>
#include <job/job.h>
#include <signal/signal.h>
#include <thread/thread.h>
#include <vat/timer.h>
#include <vat/vat.h>

#include <stdatomic.h>

#define AWAIT_SPIN_LIMIT 200000000

static void await_spin_until(const _Atomic(i32)* flag, i32 want)
{
    for (i64 i = 0; i < AWAIT_SPIN_LIMIT; i++)
    {
        if (atomic_load_explicit(flag, memory_order_acquire) == want)
            return;
        mel_thread_yield();
    }
}

static void set_done(void* user) { atomic_store_explicit((_Atomic(i32)*)user, 1, memory_order_release); }

typedef struct
{
    Mel_Future*  f;
    void*        got;
    _Atomic(i32) done;
} Await_Job;

static void await_job(void* data)
{
    Await_Job* j = (Await_Job*)data;
    j->got = mel_await_future(j->f);
    atomic_store_explicit(&j->done, 1, memory_order_release);
}

MEL_TEST(await, fiber_parks_on_pending_future_until_resolved)
{
    mel_job_init();

    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    Await_Job j = { .f = &f, .got = NULL, .done = 0 };
    mel_job_run(&j, await_job, nullptr);

    mel_thread_sleep(20 * 1000 * 1000);
    MEL_EXPECT_EQ(atomic_load(&j.done), 0);

    int value = 0x5EED;
    mel_future_resolve(&f, &value, MEL_FUTURE_OK);

    await_spin_until(&j.done, 1);
    MEL_EXPECT_EQ(j.got, &value);
    MEL_EXPECT_EQ(mel_future_status(&f), MEL_FUTURE_OK);

    mel_job_shutdown();
}

typedef struct
{
    i32               state;
    Mel_Channel*      up;
    Mel_Channel*      down;
    int               in;
    int               sq;
    Mel_Future_Status st;
} Square_Frame;

static bool square_resume(void* frame, Mel_Await_Step* out)
{
    Square_Frame* f = (Square_Frame*)frame;
    switch (f->state)
    {
    case 0:
    case 2:
        f->state = 1;
        out->channel = f->up;
        out->slot = &f->in;
        out->is_send = false;
        out->status_out = &f->st;
        return true;
    case 1:
        if (mel_future_status_failed(f->st) || f->in < 0)
            return false;
        f->sq = f->in * f->in;
        f->state = 2;
        out->channel = f->down;
        out->slot = &f->sq;
        out->is_send = true;
        return true;
    }
    return false;
}

typedef struct
{
    Mel_Channel* up;
    Mel_Channel* down;
    int          ok;
    _Atomic(i32) done;
} Mover_Job;

static bool mover_roundtrip(Mover_Job* m, int v)
{
    int reply = 0;
    if (mel_channel_send(m->up, &v) != MEL_CHANNEL_OK)
        return false;
    if (mel_channel_recv(m->down, &reply) != MEL_CHANNEL_OK)
        return false;
    return reply == v * v;
}

static void mover_job(void* data)
{
    Mover_Job* m = (Mover_Job*)data;
    int        sentinel = -1;
    m->ok = mover_roundtrip(m, 2) && mover_roundtrip(m, 3) && mover_roundtrip(m, 7);
    mel_channel_send(m->up, &sentinel);
    atomic_store_explicit(&m->done, 1, memory_order_release);
}

MEL_TEST(await, coro_task_and_fiber_bridge_both_ways_across_channels)
{
    mel_job_init();

    Mel_Channel* up = mel_channel_create(sizeof(int), 0, mel_alloc_heap());
    Mel_Channel* down = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    _Atomic(i32) coro_done = 0;
    Square_Frame frame = { .state = 0, .up = up, .down = down };

    Mel_Await_Coro c;
    mel_await_coro_start(&c,
                         (Mel_Await_Coro_Desc){
                             .frame = &frame,
                             .resume = square_resume,
                             .exec = mel_job_executor(),
                             .alloc = mel_alloc_heap(),
                             .on_done = set_done,
                             .user = &coro_done,
                         });

    Mover_Job m = { .up = up, .down = down, .ok = 0, .done = 0 };
    mel_job_run(&m, mover_job, nullptr);

    await_spin_until(&m.done, 1);
    await_spin_until(&coro_done, 1);

    MEL_EXPECT(m.ok);

    mel_channel_destroy(up);
    mel_channel_destroy(down);
    mel_job_shutdown();
}

typedef struct
{
    i32          state;
    Mel_Channel* tokens;
    int          tok;
    int          got;
    int          sum;
} Acquire_Frame;

static bool acquire_resume(void* frame, Mel_Await_Step* out)
{
    Acquire_Frame* f = (Acquire_Frame*)frame;
    if (f->state == 1)
    {
        f->got++;
        f->sum += f->tok;
        if (f->got == 3)
            return false;
    }
    f->state = 1;
    out->channel = f->tokens;
    out->slot = &f->tok;
    out->is_send = false;
    return true;
}

typedef struct
{
    Mel_Channel* tokens;
    _Atomic(i32) done;
} Producer_Job;

static void producer_job(void* data)
{
    Producer_Job* p = (Producer_Job*)data;
    for (int i = 1; i <= 3; i++)
    {
        mel_channel_send(p->tokens, &i);
        mel_job_yield();
    }
    atomic_store_explicit(&p->done, 1, memory_order_release);
}

MEL_TEST(await, coro_acquires_tokens_a_fiber_signals)
{
    mel_job_init();

    Mel_Channel* tokens = mel_channel_create(sizeof(int), 3, mel_alloc_heap());

    _Atomic(i32)  coro_done = 0;
    Acquire_Frame frame = { .state = 0, .tokens = tokens };

    Mel_Await_Coro c;
    mel_await_coro_start(&c,
                         (Mel_Await_Coro_Desc){
                             .frame = &frame,
                             .resume = acquire_resume,
                             .exec = mel_job_executor(),
                             .alloc = mel_alloc_heap(),
                             .on_done = set_done,
                             .user = &coro_done,
                         });

    Producer_Job p = { .tokens = tokens, .done = 0 };
    mel_job_run(&p, producer_job, nullptr);

    await_spin_until(&p.done, 1);
    await_spin_until(&coro_done, 1);

    MEL_EXPECT_EQ(frame.got, 3);
    MEL_EXPECT_EQ(frame.sum, 6);

    mel_channel_destroy(tokens);
    mel_job_shutdown();
}

typedef struct
{
    i32               state;
    Mel_Future*       f;
    void*             value;
    Mel_Future_Status st;
} Future_Frame;

static bool future_resume(void* frame, Mel_Await_Step* out)
{
    Future_Frame* f = (Future_Frame*)frame;
    if (f->state == 1)
    {
        f->value = mel_future_value(f->f);
        return false;
    }
    f->state = 1;
    out->future = f->f;
    out->status_out = &f->st;
    return true;
}

MEL_TEST(await, coro_awaits_future_and_reads_status)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    _Atomic(i32) coro_done = 0;
    Future_Frame frame = { .state = 0, .f = &f };

    Mel_Await_Coro c;
    mel_await_coro_start(&c,
                         (Mel_Await_Coro_Desc){
                             .frame = &frame,
                             .resume = future_resume,
                             .exec = mel_executor_inline(),
                             .on_done = set_done,
                             .user = &coro_done,
                         });

    MEL_EXPECT_EQ(atomic_load(&coro_done), 0);

    int value = 42;
    mel_future_resolve(&f, &value, MEL_FUTURE_WARNED);

    MEL_EXPECT_EQ(atomic_load(&coro_done), 1);
    MEL_EXPECT_EQ(frame.value, &value);
    MEL_EXPECT_EQ(frame.st, MEL_FUTURE_WARNED);
}

typedef struct
{
    i32 state;
    int spins;
} Yield_Frame;

static bool yield_resume(void* frame, Mel_Await_Step* out)
{
    Yield_Frame* f = (Yield_Frame*)frame;
    if (f->state == 1)
        f->spins++;
    if (f->spins == 3)
        return false;
    f->state = 1;
    out->reschedule = true;
    return true;
}

MEL_TEST(await, coro_reschedule_yields_turns_until_done)
{
    _Atomic(i32) coro_done = 0;
    Yield_Frame  frame = { 0 };

    Mel_Await_Coro c;
    mel_await_coro_start(&c,
                         (Mel_Await_Coro_Desc){
                             .frame = &frame,
                             .resume = yield_resume,
                             .exec = mel_executor_inline(),
                             .on_done = set_done,
                             .user = &coro_done,
                         });

    MEL_EXPECT_EQ(atomic_load(&coro_done), 1);
    MEL_EXPECT_EQ(frame.spins, 3);
}

typedef struct
{
    i32 state;
    int ticks;
} Sleep_Frame;

static bool sleep_resume(void* frame, Mel_Await_Step* out)
{
    Sleep_Frame* f = (Sleep_Frame*)frame;
    if (f->state == 1)
    {
        f->ticks++;
        if (f->ticks == 3)
            return false;
    }
    f->state = 1;
    out->after_ns = 2 * 1000 * 1000;
    return true;
}

typedef struct
{
    Mel_Vat*     vat;
    _Atomic(i32) done;
} Quit_On_Done;

static void quit_on_done(void* user)
{
    Quit_On_Done* q = (Quit_On_Done*)user;
    atomic_store_explicit(&q->done, 1, memory_order_release);
    mel_vat_quit(q->vat);
}

MEL_TEST(await, vat_bound_coro_retains_across_timer_sleeps)
{
    Mel_Vat_Waiter* waiter = mel_vat_waiter_kqueue(mel_alloc_heap());
    Mel_Vat_Driver* driver = mel_vat_driver_fair(mel_alloc_heap(), 64);
    Mel_Vat*        vat = mel_vat_open(mel_alloc_heap(), (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    Mel_Vat_Timers* timers = mel_vat_timers_open(vat, mel_alloc_heap());

    Quit_On_Done q = { .vat = vat, .done = 0 };
    Sleep_Frame  frame = { 0 };

    Mel_Await_Coro c;
    mel_await_coro_start(&c,
                         (Mel_Await_Coro_Desc){
                             .frame = &frame,
                             .resume = sleep_resume,
                             .exec = mel_vat_executor(vat),
                             .vat = vat,
                             .timers = timers,
                             .on_done = quit_on_done,
                             .user = &q,
                         });

    mel_vat_run(vat);

    MEL_EXPECT_EQ(atomic_load(&q.done), 1);
    MEL_EXPECT_EQ(frame.ticks, 3);

    mel_vat_timers_close(timers);
    mel_vat_run(vat);

    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
}
