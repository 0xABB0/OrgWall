#include <vat/timer.h>
#include <vat/vat.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/list.h>
#include <thread/thread.h>
#include <time/nano.h>

#include <stdatomic.h>
#include <stdio.h>
#include <sys/event.h>
#include <unistd.h>

#define TURN_ITERS 2000000
#define POST_ITERS 1000000
#define RUNS       5

typedef struct
{
    Mel_Vat* vat;
    u64      remaining;
} Spin;

static i64 spin_deadline(Mel_Vat_Source* source)
{
    MEL_UNUSED(source);
    return 0;
}

static bool spin_drain(Mel_Vat_Source* source, u32 budget)
{
    MEL_UNUSED(budget);
    Spin* s = mel_vat_source_state(source);
    if (s->remaining == 0)
    {
        mel_vat_quit(s->vat);
        return false;
    }
    s->remaining--;
    return false;
}

static const Mel_Vat_Source_Vtbl spin_vtbl = {
    .wakeables = NULL,
    .deadline = spin_deadline,
    .drain = spin_drain,
    .cancel = NULL,
};

static f64 bench_vat_turns(void)
{
    Mel_Vat_Waiter* waiter = mel_vat_waiter_kqueue(mel_alloc_heap());
    Mel_Vat_Driver* driver = mel_vat_driver_fair(mel_alloc_heap(), 1);
    Mel_Vat*        vat = mel_vat_open(mel_alloc_heap(), (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    Spin            spin = { .vat = vat, .remaining = TURN_ITERS };
    Mel_Vat_Source* source = mel_vat_source_open(vat, &spin_vtbl, &spin);

    u64 begin = mel_nanos_since_unspecified_epoch();
    mel_vat_run(vat);
    u64 end = mel_nanos_since_unspecified_epoch();

    mel_vat_source_close(source);
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
    return (f64)(end - begin) / (f64)TURN_ITERS;
}

static f64 bench_raw_kqueue_turns(void)
{
    int           kq = kqueue();
    struct kevent reg;
    EV_SET(&reg, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    kevent(kq, &reg, 1, NULL, 0, NULL);

    struct timespec zero = { 0, 0 };
    u64             begin = mel_nanos_since_unspecified_epoch();
    for (u64 i = 0; i < TURN_ITERS; i++)
    {
        struct kevent events[1];
        kevent(kq, NULL, 0, events, 1, &zero);
    }
    u64 end = mel_nanos_since_unspecified_epoch();
    close(kq);
    return (f64)(end - begin) / (f64)TURN_ITERS;
}

typedef struct
{
    Mel_Task     task;
    Mel_Vat*     vat;
    atomic_ulong received;
} Sink;

static void sink_run(Mel_Task* self)
{
    Sink* s = mel_container_of(self, Sink, task);
    atomic_fetch_add_explicit(&s->received, 1, memory_order_relaxed);
}

typedef struct
{
    Mel_Vat* vat;
    Sink*    sinks;
    u64      count;
    Mel_Task quit_task;
} Producer;

static void producer_quit_run(Mel_Task* self)
{
    Producer* p = mel_container_of(self, Producer, quit_task);
    mel_vat_quit(p->vat);
}

static int producer_thread(void* user)
{
    Producer* p = user;
    for (u64 i = 0; i < p->count; i++)
    {
        Sink* s = &p->sinks[i % 64];
        mel_vat_post(p->vat, &s->task);
    }
    mel_vat_post(p->vat, &p->quit_task);
    return 0;
}

static f64 bench_cross_thread_post(void)
{
    Mel_Vat_Waiter* waiter = mel_vat_waiter_kqueue(mel_alloc_heap());
    Mel_Vat_Driver* driver = mel_vat_driver_fair(mel_alloc_heap(), 4096);
    Mel_Vat*        vat = mel_vat_open(mel_alloc_heap(), (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    Mel_Vat_Timers* timers = mel_vat_timers_open(vat, mel_alloc_heap());

    Sink* sinks = mel_alloc_array(mel_alloc_heap(), Sink, 64);
    for (usize i = 0; i < 64; i++)
    {
        sinks[i].vat = vat;
        atomic_init(&sinks[i].received, 0);
        mel_task_init(&sinks[i].task, sink_run);
    }
    Producer producer = { .vat = vat, .sinks = sinks, .count = POST_ITERS };
    mel_task_init(&producer.quit_task, producer_quit_run);

    Mel_Thread thread;
    u64        begin = mel_nanos_since_unspecified_epoch();
    mel_thread_spawn(&thread, producer_thread, &producer);
    mel_vat_run(vat);
    u64 end = mel_nanos_since_unspecified_epoch();
    mel_thread_join(&thread, NULL);

    mel_dealloc(mel_alloc_heap(), sinks);
    mel_vat_timers_close(timers);
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
    return (f64)POST_ITERS / ((f64)(end - begin) / 1e9) / 1e6;
}

int main(void)
{
    printf("vat bench — %d turn iters, %d post iters, %d runs\n", TURN_ITERS, POST_ITERS, RUNS);
    for (int run = 0; run < RUNS; run++)
    {
        f64 vat_ns = bench_vat_turns();
        f64 raw_ns = bench_raw_kqueue_turns();
        f64 posts = bench_cross_thread_post();
        printf("run %d: turn %.1f ns/iter (raw kqueue %.1f, overhead %.1f ns, %.1f%%), cross-post %.2f M/s\n", run, vat_ns, raw_ns, vat_ns - raw_ns, (vat_ns - raw_ns) / raw_ns * 100.0, posts);
    }
    return 0;
}
