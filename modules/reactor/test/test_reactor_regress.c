#include <reactor/reactor.h>
#include <executor/executor.h>
#include <test/test.h>

#include <collection/list.h>
#include <thread/thread.h>
#include <time/nano.h>

#include <stdatomic.h>
#include <string.h>

typedef struct
{
    Mel_Reactor* reactor;
    int          timer_fires;
    int          fires_to_quit;
    i64          interval_ns;
    i64          first_fire_ns;
    i64          last_fire_ns;
} Cadence_Ctx;

static bool cadence_timer(void* user)
{
    Cadence_Ctx* c = (Cadence_Ctx*)user;
    i64          now = (i64)mel_nanos_since_unspecified_epoch();
    if (c->timer_fires == 0)
        c->first_fire_ns = now;
    c->last_fire_ns = now;
    c->timer_fires++;
    if (c->timer_fires >= c->fires_to_quit)
        mel_reactor_quit(c->reactor);
    return true;
}

static bool cadence_init(Mel_Reactor* r, void* user)
{
    Cadence_Ctx* c = (Cadence_Ctx*)user;
    c->reactor = r;
    Mel_Reactor_Source* timer = mel_reactor_timer_new(c->interval_ns, cadence_timer, c);
    mel_reactor_source_attach(r, timer);
    return true;
}

MEL_TEST(reactor_regress, timer_fires_at_its_cadence)
{
    Cadence_Ctx c = { 0 };
    c.interval_ns = (i64)5 * 1000 * 1000;
    c.fires_to_quit = 10;
    mel_reactor_spawn(MEL_REACTOR_THREADED, cadence_init, &c);

    MEL_EXPECT_EQ(c.timer_fires, 10);
    i64 span = c.last_fire_ns - c.first_fire_ns;
    i64 expected = c.interval_ns * (c.fires_to_quit - 1);
    MEL_EXPECT_GE(span, expected * 8 / 10);
    MEL_EXPECT_LE(span, expected * 4);
}

typedef struct
{
    Mel_Reactor* reactor;
    int          idle_turns;
    int          quit_after;
} IdleEvery_Ctx;

static bool idle_every_cb(void* user)
{
    IdleEvery_Ctx* c = (IdleEvery_Ctx*)user;
    c->idle_turns++;
    if (c->idle_turns >= c->quit_after)
        mel_reactor_quit(c->reactor);
    return true;
}

static bool idle_every_init(Mel_Reactor* r, void* user)
{
    IdleEvery_Ctx* c = (IdleEvery_Ctx*)user;
    c->reactor = r;
    mel_reactor_source_attach(r, mel_reactor_idle_new(idle_every_cb, c));
    return true;
}

MEL_TEST(reactor_regress, idle_runs_every_turn)
{
    IdleEvery_Ctx c = { 0 };
    c.quit_after = 500;
    mel_reactor_spawn(MEL_REACTOR_THREADED, idle_every_init, &c);

    MEL_EXPECT_EQ(c.idle_turns, 500);
}

typedef struct
{
    Mel_Reactor* reactor;
    int          turn;
    int          quit_turn;

    Mel_Reactor_Source* victim;
    int                 victim_runs;
    int                 attach_on_turn;
    int                 detach_on_turn;
    bool                detached;

    Mel_Reactor_Source* late;
    int                 late_runs;
    int                 late_attached_turn;
} Lifecycle_Ctx;

static bool victim_cb(void* user)
{
    Lifecycle_Ctx* c = (Lifecycle_Ctx*)user;
    c->victim_runs++;
    return true;
}

static bool late_cb(void* user)
{
    Lifecycle_Ctx* c = (Lifecycle_Ctx*)user;
    c->late_runs++;
    return true;
}

static bool lifecycle_driver(void* user)
{
    Lifecycle_Ctx* c = (Lifecycle_Ctx*)user;
    c->turn++;

    if (c->turn == c->attach_on_turn)
    {
        c->late = mel_reactor_idle_new(late_cb, c);
        c->late_attached_turn = c->turn;
        mel_reactor_source_attach(c->reactor, c->late);
    }

    if (c->turn == c->detach_on_turn && !c->detached)
    {
        mel_reactor_source_detach(c->victim);
        c->detached = true;
    }

    if (c->turn >= c->quit_turn)
        mel_reactor_quit(c->reactor);
    return true;
}

static bool lifecycle_init(Mel_Reactor* r, void* user)
{
    Lifecycle_Ctx* c = (Lifecycle_Ctx*)user;
    c->reactor = r;
    Mel_Reactor_Source* driver = mel_reactor_idle_new(lifecycle_driver, c);
    mel_reactor_source_attach(r, driver);
    c->victim = mel_reactor_idle_new(victim_cb, c);
    mel_reactor_source_attach(r, c->victim);
    return true;
}

MEL_TEST(reactor_regress, detach_mid_dispatch_is_honored_deferred)
{
    Lifecycle_Ctx c = { 0 };
    c.attach_on_turn = 3;
    c.detach_on_turn = 5;
    c.quit_turn = 12;
    mel_reactor_spawn(MEL_REACTOR_THREADED, lifecycle_init, &c);

    MEL_EXPECT_GT(c.victim_runs, 0);
    MEL_EXPECT_LE(c.victim_runs, c.detach_on_turn);
    MEL_EXPECT_GT(c.late_runs, 0);
    MEL_EXPECT_LE(c.late_runs, c.quit_turn - c.late_attached_turn);
}

typedef struct
{
    Mel_Reactor* reactor;
    int          turn;
    int          quit_turn;

    Mel_Reactor_Source* one_shot;
    int                 one_shot_runs;
    int                 one_shot_turn;
} Oneshot_Ctx;

static bool one_shot_cb(void* user)
{
    Oneshot_Ctx* c = (Oneshot_Ctx*)user;
    c->one_shot_runs++;
    c->one_shot_turn = c->turn;
    return false;
}

static bool oneshot_driver(void* user)
{
    Oneshot_Ctx* c = (Oneshot_Ctx*)user;
    c->turn++;
    if (c->turn >= c->quit_turn)
        mel_reactor_quit(c->reactor);
    return true;
}

static bool oneshot_init(Mel_Reactor* r, void* user)
{
    Oneshot_Ctx* c = (Oneshot_Ctx*)user;
    c->reactor = r;
    Mel_Reactor_Source* driver = mel_reactor_idle_new(oneshot_driver, c);
    mel_reactor_source_attach(r, driver);
    c->one_shot = mel_reactor_idle_new(one_shot_cb, c);
    mel_reactor_source_attach(r, c->one_shot);
    return true;
}

MEL_TEST(reactor_regress, false_return_destroys_source_once)
{
    Oneshot_Ctx c = { 0 };
    c.quit_turn = 30;
    mel_reactor_spawn(MEL_REACTOR_THREADED, oneshot_init, &c);

    MEL_EXPECT_EQ(c.one_shot_runs, 1);
}

typedef struct
{
    _Atomic(Mel_Reactor*) reactor;
    _Atomic(int)          ready;
    _Atomic(int)          owner_inline_runs;
    _Atomic(int)          cross_runs;
    int                   turn;
    Mel_Task              quit_task;
} Post_Ctx;

static void post_inline_cb(void* user)
{
    Post_Ctx* p = (Post_Ctx*)user;
    atomic_fetch_add(&p->owner_inline_runs, 1);
}

static void post_cross_cb(void* user)
{
    Post_Ctx* p = (Post_Ctx*)user;
    atomic_fetch_add(&p->cross_runs, 1);
}

static void post_quit_run(Mel_Task* self)
{
    Post_Ctx* p = mel_container_of(self, Post_Ctx, quit_task);
    mel_reactor_quit((Mel_Reactor*)atomic_load(&p->reactor));
}

static bool post_driver(void* user)
{
    Post_Ctx* p = (Post_Ctx*)user;
    p->turn++;
    if (p->turn == 1)
        mel_reactor_post((Mel_Reactor*)atomic_load(&p->reactor), post_inline_cb, p);
    return true;
}

static bool post_init(Mel_Reactor* r, void* user)
{
    Post_Ctx* p = (Post_Ctx*)user;
    atomic_store(&p->reactor, r);
    Mel_Reactor_Source* driver = mel_reactor_idle_new(post_driver, p);
    mel_reactor_source_attach(r, driver);
    atomic_store(&p->ready, 1);
    return true;
}

static int post_loop_thread(void* user)
{
    Post_Ctx* p = (Post_Ctx*)user;
    mel_reactor_spawn(MEL_REACTOR_THREADED, post_init, p);
    return 0;
}

MEL_TEST(reactor_regress, post_inline_on_owner_queued_from_other_thread)
{
    Post_Ctx p = { 0 };
    mel_task_init(&p.quit_task, post_quit_run);

    Mel_Thread th = { 0 };
    bool       spawned = mel_thread_spawn(&th, post_loop_thread, &p);
    MEL_REQUIRE(spawned);

    while (atomic_load(&p.ready) == 0)
        mel_thread_yield();
    mel_thread_sleep((i64)20 * 1000 * 1000);

    Mel_Reactor* r = (Mel_Reactor*)atomic_load(&p.reactor);
    MEL_REQUIRE_NOT_NULL(r);

    for (int i = 0; i < 50; i++)
        mel_reactor_post(r, post_cross_cb, &p);

    mel_reactor_defer(r, &p.quit_task);

    int code = 0;
    mel_thread_join(&th, &code);

    MEL_EXPECT_EQ(atomic_load(&p.owner_inline_runs), 1);
    MEL_EXPECT_EQ(atomic_load(&p.cross_runs), 50);
}

#define STRESS_THREADS 8
#define STRESS_TASKS   256

typedef struct
{
    Mel_Task     task;
    _Atomic(int) runs;
    void*        ctx;
} Stress_Probe;

typedef struct
{
    _Atomic(Mel_Reactor*) reactor;
    _Atomic(int)          ready;
    _Atomic(int)          total_runs;
    _Atomic(i64)          witness_fires;
    _Atomic(int)          go;
    _Atomic(int)          producers_done;
    Stress_Probe          probes[STRESS_TASKS];
    Mel_Task              quit_task;
} Stress_Ctx;

static void stress_probe_run(Mel_Task* self)
{
    Stress_Probe* p = mel_container_of(self, Stress_Probe, task);
    atomic_fetch_add_explicit(&p->runs, 1, memory_order_relaxed);
    Stress_Ctx* c = (Stress_Ctx*)p->ctx;
    atomic_fetch_add_explicit(&c->total_runs, 1, memory_order_relaxed);
}

static void stress_quit_run(Mel_Task* self)
{
    Stress_Ctx* c = mel_container_of(self, Stress_Ctx, quit_task);
    mel_reactor_quit((Mel_Reactor*)atomic_load(&c->reactor));
}

static bool stress_witness(void* user)
{
    Stress_Ctx* c = (Stress_Ctx*)user;
    atomic_fetch_add_explicit(&c->witness_fires, 1, memory_order_relaxed);
    return true;
}

static bool stress_init(Mel_Reactor* r, void* user)
{
    Stress_Ctx* c = (Stress_Ctx*)user;
    atomic_store(&c->reactor, r);
    Mel_Reactor_Source* witness = mel_reactor_timer_new((i64)10 * 1000 * 1000 * 1000, stress_witness, c);
    mel_reactor_source_attach(r, witness);
    atomic_store(&c->ready, 1);
    return true;
}

static int stress_loop_thread(void* user)
{
    Stress_Ctx* c = (Stress_Ctx*)user;
    mel_reactor_spawn(MEL_REACTOR_THREADED, stress_init, c);
    return 0;
}

typedef struct
{
    Stress_Ctx* ctx;
    int         lo;
    int         hi;
} Stress_Range;

static int stress_producer(void* user)
{
    Stress_Range* rg = (Stress_Range*)user;
    Stress_Ctx*   c = rg->ctx;
    while (atomic_load(&c->go) == 0)
        mel_thread_yield();
    Mel_Reactor* r = (Mel_Reactor*)atomic_load(&c->reactor);
    for (int rep = 0; rep < 4; rep++)
    {
        for (int i = rg->lo; i < rg->hi; i++)
            mel_reactor_defer(r, &c->probes[i].task);
        mel_thread_sleep((i64)1 * 1000 * 1000);
    }
    atomic_fetch_add(&c->producers_done, 1);
    return 0;
}

MEL_TEST(reactor_regress, multithread_defer_runs_each_task_exactly_once_per_arm)
{
    static Stress_Ctx c;
    memset(&c, 0, sizeof c);
    mel_task_init(&c.quit_task, stress_quit_run);
    for (int i = 0; i < STRESS_TASKS; i++)
    {
        mel_task_init(&c.probes[i].task, stress_probe_run);
        c.probes[i].ctx = &c;
    }

    Mel_Thread loop = { 0 };
    MEL_REQUIRE(mel_thread_spawn(&loop, stress_loop_thread, &c));
    while (atomic_load(&c.ready) == 0)
        mel_thread_yield();

    Mel_Thread   prod[STRESS_THREADS] = { 0 };
    Stress_Range rng[STRESS_THREADS] = { 0 };
    int          per = STRESS_TASKS / STRESS_THREADS;
    for (int t = 0; t < STRESS_THREADS; t++)
    {
        rng[t].ctx = &c;
        rng[t].lo = t * per;
        rng[t].hi = (t == STRESS_THREADS - 1) ? STRESS_TASKS : (t + 1) * per;
        MEL_REQUIRE(mel_thread_spawn(&prod[t], stress_producer, &rng[t]));
    }

    atomic_store(&c.go, 1);

    while (atomic_load(&c.producers_done) < STRESS_THREADS)
        mel_thread_yield();

    mel_thread_sleep((i64)100 * 1000 * 1000);

    i64          witness_idle = atomic_load(&c.witness_fires);
    Mel_Reactor* r = (Mel_Reactor*)atomic_load(&c.reactor);
    mel_reactor_defer(r, &c.quit_task);

    int code = 0;
    for (int t = 0; t < STRESS_THREADS; t++)
        mel_thread_join(&prod[t], &code);
    mel_thread_join(&loop, &code);

    int min_runs = c.probes[0].runs;
    int max_runs = c.probes[0].runs;
    for (int i = 0; i < STRESS_TASKS; i++)
    {
        int rn = atomic_load(&c.probes[i].runs);
        if (rn < min_runs)
            min_runs = rn;
        if (rn > max_runs)
            max_runs = rn;
    }

    MEL_EXPECT_GE(min_runs, 1);
    MEL_EXPECT_LE(max_runs, 16);
    MEL_EXPECT_EQ(witness_idle, (i64)0);
}
