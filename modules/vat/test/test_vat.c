#include <vat/timer.h>
#include <vat/vat.h>

#include <test/test.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/list.h>
#include <thread/thread.h>
#include <time/nano.h>

#include <stdatomic.h>
#include <unistd.h>

typedef struct
{
    Mel_Vat*        vat;
    Mel_Vat_Waiter* waiter;
    Mel_Vat_Driver* driver;
} Fixture;

static Fixture fixture_open(void)
{
    Fixture f;
    f.waiter = mel_vat_waiter_kqueue(mel_alloc_heap());
    f.driver = mel_vat_driver_fair(mel_alloc_heap(), 64);
    f.vat = mel_vat_open(mel_alloc_heap(), (Mel_Vat_Desc){ .waiter = f.waiter, .driver = f.driver });
    return f;
}

static void fixture_close(Fixture* f)
{
    mel_vat_close(f->vat);
    f->driver->vt->close(f->driver);
    f->waiter->vt->close(f->waiter);
}

typedef struct
{
    Mel_Task task;
    int      ran;
    int      order;
} Probe;

static int g_order;

static void probe_run(Mel_Task* self)
{
    Probe* p = mel_container_of(self, Probe, task);
    p->ran++;
    p->order = g_order++;
}

MEL_TEST(vat, runs_posted_tasks_then_returns_unretained)
{
    Fixture f = fixture_open();
    Probe   a = { 0 };
    Probe   b = { 0 };
    g_order = 0;
    mel_task_init(&a.task, probe_run);
    mel_task_init(&b.task, probe_run);

    mel_vat_post(f.vat, &a.task);
    mel_vat_post(f.vat, &b.task);
    mel_vat_run(f.vat);

    MEL_EXPECT_EQ(a.ran, 1);
    MEL_EXPECT_EQ(b.ran, 1);
    MEL_EXPECT_LT(a.order, b.order);
    fixture_close(&f);
}

MEL_TEST(vat, post_coalesces_while_armed)
{
    Fixture f = fixture_open();
    Probe   p = { 0 };
    mel_task_init(&p.task, probe_run);

    mel_vat_post(f.vat, &p.task);
    mel_vat_post(f.vat, &p.task);
    mel_vat_run(f.vat);

    MEL_EXPECT_EQ(p.ran, 1);
    fixture_close(&f);
}

typedef struct
{
    Mel_Task        task;
    Mel_Vat*        vat;
    Mel_Vat_Timers* timers;
    int             fired;
    int             order;
} Timed;

static void timed_run(Mel_Task* self)
{
    Timed* t = mel_container_of(self, Timed, task);
    t->fired++;
    t->order = g_order++;
    if (mel_vat_timers_pending(t->timers) == 0)
        mel_vat_quit(t->vat);
}

MEL_TEST(vat, timers_fire_in_deadline_order)
{
    Fixture         f = fixture_open();
    Mel_Vat_Timers* timers = mel_vat_timers_open(f.vat, mel_alloc_heap());
    i64             now = (i64)mel_nanos_since_unspecified_epoch();
    Timed           late = { .vat = f.vat, .timers = timers };
    Timed           soon = { .vat = f.vat, .timers = timers };
    g_order = 0;
    mel_task_init(&late.task, timed_run);
    mel_task_init(&soon.task, timed_run);

    mel_vat_timers_add(timers, now + 20 * 1000 * 1000, &late.task);
    mel_vat_timers_add(timers, now + 5 * 1000 * 1000, &soon.task);
    mel_vat_run(f.vat);

    MEL_EXPECT_EQ(soon.fired, 1);
    MEL_EXPECT_EQ(late.fired, 1);
    MEL_EXPECT_LT(soon.order, late.order);
    mel_vat_timers_close(timers);
    fixture_close(&f);
}

typedef struct
{
    Mel_Task task;
    Mel_Vat* vat;
    int      ran;
} Quitter;

static void quitter_run(Mel_Task* self)
{
    Quitter* q = mel_container_of(self, Quitter, task);
    q->ran++;
    mel_vat_quit(q->vat);
}

static int cross_post_thread(void* user)
{
    Quitter* q = user;
    mel_thread_sleep(5 * 1000 * 1000);
    mel_vat_post(q->vat, &q->task);
    return 0;
}

MEL_TEST(vat, cross_thread_post_wakes_parked_vat)
{
    Fixture         f = fixture_open();
    Mel_Vat_Timers* timers = mel_vat_timers_open(f.vat, mel_alloc_heap());
    Quitter         q = { .vat = f.vat };
    mel_task_init(&q.task, quitter_run);

    Mel_Thread thread;
    MEL_REQUIRE(mel_thread_spawn(&thread, cross_post_thread, &q));
    mel_vat_run(f.vat);
    mel_thread_join(&thread, NULL);

    MEL_EXPECT_EQ(q.ran, 1);
    mel_vat_timers_close(timers);
    fixture_close(&f);
}

typedef struct
{
    Mel_Task task;
    Mel_Vat* vat;
    Quitter  inner;
    i32      depth_seen;
    int      ran;
} Nester;

static void nester_run(Mel_Task* self)
{
    Nester* n = mel_container_of(self, Nester, task);
    n->ran++;
    mel_vat_post(n->vat, &n->inner.task);
    mel_vat_run(n->vat);
    n->depth_seen = mel_vat_depth(n->vat);
    mel_vat_quit(n->vat);
}

MEL_TEST(vat, nested_run_inner_quit_leaves_outer_alive)
{
    Fixture         f = fixture_open();
    Mel_Vat_Timers* timers = mel_vat_timers_open(f.vat, mel_alloc_heap());
    Nester          n = { .vat = f.vat };
    n.inner.vat = f.vat;
    mel_task_init(&n.task, nester_run);
    mel_task_init(&n.inner.task, quitter_run);

    mel_vat_post(f.vat, &n.task);
    mel_vat_run(f.vat);

    MEL_EXPECT_EQ(n.ran, 1);
    MEL_EXPECT_EQ(n.inner.ran, 1);
    MEL_EXPECT_EQ(n.depth_seen, 1);
    mel_vat_timers_close(timers);
    fixture_close(&f);
}

MEL_TEST(vat, executor_adapter_submits_to_vat)
{
    Fixture       f = fixture_open();
    Mel_Executor* exec = mel_vat_executor(f.vat);
    Probe         p = { 0 };
    mel_task_init(&p.task, probe_run);

    mel_executor_submit(exec, &p.task);
    mel_vat_run(f.vat);

    MEL_EXPECT_EQ(p.ran, 1);
    fixture_close(&f);
}

typedef struct
{
    Mel_Vat_Waiter base;
    i64            last_timeout;
    int            waits;
    int            rings;
} Spy_Waiter;

static bool spy_arm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    MEL_UNUSED(waiter);
    MEL_UNUSED(wakeable);
    return true;
}

static void spy_disarm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    MEL_UNUSED(waiter);
    MEL_UNUSED(wakeable);
}

static i32 spy_wait(Mel_Vat_Waiter* waiter, i64 timeout_ns)
{
    Spy_Waiter* spy = mel_container_of(waiter, Spy_Waiter, base);
    spy->last_timeout = timeout_ns;
    spy->waits++;
    return 0;
}

static void spy_ring(Mel_Vat_Waiter* waiter)
{
    Spy_Waiter* spy = mel_container_of(waiter, Spy_Waiter, base);
    spy->rings++;
}

static void spy_close(Mel_Vat_Waiter* waiter) { MEL_UNUSED(waiter); }

static const Mel_Vat_Waiter_Vtbl spy_vtbl = { spy_arm, spy_disarm, spy_wait, spy_ring, spy_close };

MEL_TEST(vat, reduction_blocks_forever_without_deadlines)
{
    Spy_Waiter      spy = { .base = { &spy_vtbl }, .last_timeout = -2 };
    Mel_Vat_Driver* driver = mel_vat_driver_fair(mel_alloc_heap(), 64);
    Mel_Vat*        vat = mel_vat_open(mel_alloc_heap(), (Mel_Vat_Desc){ .waiter = &spy.base, .driver = driver });
    Mel_Vat_Timers* timers = mel_vat_timers_open(vat, mel_alloc_heap());

    MEL_EXPECT(mel_vat_step(vat));
    MEL_EXPECT_EQ(spy.waits, 1);
    MEL_EXPECT_EQ(spy.last_timeout, -1);

    mel_vat_timers_close(timers);
    mel_vat_close(vat);
    driver->vt->close(driver);
}

MEL_TEST(vat, reduction_times_wait_to_nearest_deadline)
{
    Spy_Waiter      spy = { .base = { &spy_vtbl }, .last_timeout = -2 };
    Mel_Vat_Driver* driver = mel_vat_driver_fair(mel_alloc_heap(), 64);
    Mel_Vat*        vat = mel_vat_open(mel_alloc_heap(), (Mel_Vat_Desc){ .waiter = &spy.base, .driver = driver });
    Mel_Vat_Timers* timers = mel_vat_timers_open(vat, mel_alloc_heap());
    Probe           p = { 0 };
    mel_task_init(&p.task, probe_run);

    i64 now = (i64)mel_nanos_since_unspecified_epoch();
    mel_vat_timers_add(timers, now + 100 * 1000 * 1000, &p.task);
    MEL_EXPECT(mel_vat_step(vat));

    MEL_EXPECT_GT(spy.last_timeout, 0);
    MEL_EXPECT_LE(spy.last_timeout, 100 * 1000 * 1000);

    mel_vat_timers_close(timers);
    mel_vat_close(vat);
    driver->vt->close(driver);
}

typedef struct
{
    Mel_Vat_Wakeable wakeable;
    Mel_Vat_Source*  source;
    Mel_Vat*         vat;
    int              fd;
    int              reads;
    char             received;
} Pipe_State;

static void pipe_wakeables(Mel_Vat_Source* source, Mel_Vat_Wakeable** out, usize* count)
{
    Pipe_State* ps = mel_vat_source_state(source);
    *out = &ps->wakeable;
    *count = 1;
}

static bool pipe_drain(Mel_Vat_Source* source, u32 budget)
{
    MEL_UNUSED(budget);
    Pipe_State* ps = mel_vat_source_state(source);
    char        c = 0;
    if (read(ps->fd, &c, 1) == 1)
    {
        ps->reads++;
        ps->received = c;
    }
    mel_vat_quit(ps->vat);
    return false;
}

static const Mel_Vat_Source_Vtbl pipe_vtbl = {
    .wakeables = pipe_wakeables,
    .deadline = NULL,
    .drain = pipe_drain,
    .cancel = NULL,
};

MEL_TEST(vat, fd_readiness_drains_source)
{
    Fixture f = fixture_open();
    int     fds[2];
    MEL_REQUIRE_EQ(pipe(fds), 0);

    Pipe_State ps = { 0 };
    ps.vat = f.vat;
    ps.fd = fds[0];
    ps.wakeable.handle = fds[0];
    ps.wakeable.events = MEL_VAT_WAKE_IN;
    ps.source = mel_vat_source_open(f.vat, &pipe_vtbl, &ps);

    char x = 'x';
    MEL_REQUIRE_EQ(write(fds[1], &x, 1), 1);
    mel_vat_run(f.vat);

    MEL_EXPECT_EQ(ps.reads, 1);
    MEL_EXPECT_EQ(ps.received, 'x');
    mel_vat_source_close(ps.source);
    close(fds[0]);
    close(fds[1]);
    fixture_close(&f);
}

MEL_TEST(vat, cocoa_waiter_bridges_fd_readiness)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Vat_Waiter*  waiter = mel_vat_waiter_cocoa(a);
    Mel_Vat_Driver*  driver = mel_vat_driver_fair(a, 64);
    Mel_Vat*         vat = mel_vat_open(a, (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    int              fds[2];
    MEL_REQUIRE_EQ(pipe(fds), 0);

    Pipe_State ps = { 0 };
    ps.vat = vat;
    ps.fd = fds[0];
    ps.wakeable.handle = fds[0];
    ps.wakeable.events = MEL_VAT_WAKE_IN;
    ps.source = mel_vat_source_open(vat, &pipe_vtbl, &ps);

    char y = 'y';
    MEL_REQUIRE_EQ(write(fds[1], &y, 1), 1);
    mel_vat_run(vat);

    MEL_EXPECT_EQ(ps.reads, 1);
    MEL_EXPECT_EQ(ps.received, 'y');
    mel_vat_source_close(ps.source);
    close(fds[0]);
    close(fds[1]);
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
}
