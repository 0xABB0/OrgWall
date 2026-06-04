#include <reactor/reactor.h>
#include <executor/executor.h>
#include <test/test.h>

#include <collection/list.h>
#include <thread/thread.h>
#include <time/nano.h>

#include <stdatomic.h>

typedef struct
{
    Mel_Task task;
    int      run_count;
    int      run_turn;
    void*    ctx;
} Probe;

typedef struct
{
    Mel_Reactor* reactor;
    int          turn;

    Probe a;
    Probe b;
    Probe c;

    int  defer_turn;
    bool ran_inline;
    int  inline_seen_count;
    int  run_seen_at_turn;

    int  quit_after_turn;
    int  arm_on_turn;
    bool use_executor;

    int self_redefer_left;
} Defer_Ctx;

static void probe_run(Mel_Task* self)
{
    Probe*     p = mel_container_of(self, Probe, task);
    Defer_Ctx* c = (Defer_Ctx*)p->ctx;
    p->run_count++;
    p->run_turn = c->turn;
}

static void probe_init(Probe* p, Defer_Ctx* c)
{
    mel_task_init(&p->task, probe_run);
    p->run_count = 0;
    p->run_turn = -1;
    p->ctx = c;
}

static void defer_one(Defer_Ctx* c, Probe* p)
{
    if (c->use_executor)
        mel_executor_submit(mel_reactor_executor(c->reactor), &p->task);
    else
        mel_reactor_defer(c->reactor, &p->task);
}

static bool order_idle(void* user)
{
    Defer_Ctx* c = (Defer_Ctx*)user;

    if (c->a.run_count > 0 && c->run_seen_at_turn < 0)
        c->run_seen_at_turn = c->turn;

    c->turn++;

    if (c->turn == c->arm_on_turn)
    {
        c->defer_turn = c->turn;
        int before = c->a.run_count;
        defer_one(c, &c->a);
        c->inline_seen_count = c->a.run_count - before;
        c->ran_inline = c->a.run_count > before;
    }

    if (c->turn >= c->quit_after_turn)
        mel_reactor_quit(c->reactor);
    return true;
}

static bool order_init(Mel_Reactor* r, void* user)
{
    Defer_Ctx* c = (Defer_Ctx*)user;
    c->reactor = r;
    c->run_seen_at_turn = -1;
    probe_init(&c->a, c);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(order_idle, c);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(reactor_defer, loop_thread_defer_runs_next_turn_not_inline)
{
    Defer_Ctx c = { 0 };
    c.arm_on_turn = 2;
    c.quit_after_turn = 6;
    mel_reactor_spawn(MEL_REACTOR_THREADED, order_init, &c);

    MEL_EXPECT_EQ(c.ran_inline, false);
    MEL_EXPECT_EQ(c.inline_seen_count, 0);
    MEL_EXPECT_EQ(c.a.run_count, 1);
    MEL_EXPECT_EQ(c.a.run_turn, c.defer_turn);
    MEL_EXPECT_EQ(c.run_seen_at_turn, c.defer_turn);
}

MEL_TEST(reactor_defer, executor_submit_runs_next_turn_not_inline)
{
    Defer_Ctx c = { 0 };
    c.use_executor = true;
    c.arm_on_turn = 2;
    c.quit_after_turn = 6;
    mel_reactor_spawn(MEL_REACTOR_THREADED, order_init, &c);

    MEL_EXPECT_EQ(c.ran_inline, false);
    MEL_EXPECT_EQ(c.a.run_count, 1);
    MEL_EXPECT_EQ(c.a.run_turn, c.defer_turn);
    MEL_EXPECT_EQ(c.run_seen_at_turn, c.defer_turn);
}

static _Atomic(i64) g_fifo_seq;

static void fifo_run(Mel_Task* self)
{
    Probe* p = mel_container_of(self, Probe, task);
    p->run_count++;
    p->run_turn = (int)atomic_fetch_add_explicit(&g_fifo_seq, 1, memory_order_relaxed);
}

static bool fifo_idle(void* user)
{
    Defer_Ctx* c = (Defer_Ctx*)user;
    c->turn++;
    if (c->turn == c->arm_on_turn)
    {
        defer_one(c, &c->a);
        defer_one(c, &c->b);
        defer_one(c, &c->c);
    }
    if (c->turn >= c->quit_after_turn)
        mel_reactor_quit(c->reactor);
    return true;
}

static bool fifo_init(Mel_Reactor* r, void* user)
{
    Defer_Ctx* c = (Defer_Ctx*)user;
    c->reactor = r;
    probe_init(&c->a, c);
    probe_init(&c->b, c);
    probe_init(&c->c, c);
    c->a.task.run = fifo_run;
    c->b.task.run = fifo_run;
    c->c.task.run = fifo_run;
    Mel_Reactor_Source* idle = mel_reactor_idle_new(fifo_idle, c);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(reactor_defer, multiple_defers_run_fifo_next_turn)
{
    atomic_store(&g_fifo_seq, 0);
    Defer_Ctx c = { 0 };
    c.arm_on_turn = 2;
    c.quit_after_turn = 6;
    mel_reactor_spawn(MEL_REACTOR_THREADED, fifo_init, &c);

    MEL_EXPECT_EQ(c.a.run_count, 1);
    MEL_EXPECT_EQ(c.b.run_count, 1);
    MEL_EXPECT_EQ(c.c.run_count, 1);
    MEL_EXPECT_EQ(c.a.run_turn, 0);
    MEL_EXPECT_EQ(c.b.run_turn, 1);
    MEL_EXPECT_EQ(c.c.run_turn, 2);
}

static bool coalesce_idle(void* user)
{
    Defer_Ctx* c = (Defer_Ctx*)user;
    c->turn++;
    if (c->turn == c->arm_on_turn)
    {
        c->defer_turn = c->turn;
        for (int i = 0; i < 1000; i++)
            defer_one(c, &c->a);
    }
    if (c->turn >= c->quit_after_turn)
        mel_reactor_quit(c->reactor);
    return true;
}

static bool coalesce_init(Mel_Reactor* r, void* user)
{
    Defer_Ctx* c = (Defer_Ctx*)user;
    c->reactor = r;
    probe_init(&c->a, c);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(coalesce_idle, c);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(reactor_defer, armed_coalesces_repeated_defer_of_same_task)
{
    Defer_Ctx c = { 0 };
    c.arm_on_turn = 2;
    c.quit_after_turn = 6;
    mel_reactor_spawn(MEL_REACTOR_THREADED, coalesce_init, &c);

    MEL_EXPECT_EQ(c.a.run_count, 1);
    MEL_EXPECT_EQ(c.a.run_turn, c.defer_turn);
}

static void self_redefer_run(Mel_Task* self)
{
    Probe*     p = mel_container_of(self, Probe, task);
    Defer_Ctx* c = (Defer_Ctx*)p->ctx;
    p->run_count++;
    p->run_turn = c->turn;
    if (c->self_redefer_left > 0)
    {
        c->self_redefer_left--;
        mel_reactor_defer(c->reactor, &p->task);
    }
}

static bool self_redefer_idle(void* user)
{
    Defer_Ctx* c = (Defer_Ctx*)user;
    c->turn++;
    if (c->turn == c->arm_on_turn)
        mel_reactor_defer(c->reactor, &c->a.task);
    if (c->turn >= c->quit_after_turn)
        mel_reactor_quit(c->reactor);
    return true;
}

static bool self_redefer_init(Mel_Reactor* r, void* user)
{
    Defer_Ctx* c = (Defer_Ctx*)user;
    c->reactor = r;
    probe_init(&c->a, c);
    c->a.task.run = self_redefer_run;
    Mel_Reactor_Source* idle = mel_reactor_idle_new(self_redefer_idle, c);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(reactor_defer, self_redefer_runs_next_turn_not_recursively)
{
    Defer_Ctx c = { 0 };
    c.arm_on_turn = 2;
    c.self_redefer_left = 3;
    c.quit_after_turn = 12;
    mel_reactor_spawn(MEL_REACTOR_THREADED, self_redefer_init, &c);

    MEL_EXPECT_EQ(c.a.run_count, 4);
    MEL_EXPECT_EQ(c.a.run_turn, c.arm_on_turn + 3);
}

typedef struct
{
    _Atomic(Mel_Reactor*) reactor;
    _Atomic(int)          ready;
    _Atomic(int)          ran;
    Mel_Task              task;
} Wake_Ctx;

static void wake_task_run(Mel_Task* self)
{
    Wake_Ctx* w = mel_container_of(self, Wake_Ctx, task);
    atomic_store(&w->ran, 1);
    mel_reactor_quit((Mel_Reactor*)atomic_load(&w->reactor));
}

static bool wake_init(Mel_Reactor* r, void* user)
{
    Wake_Ctx* w = (Wake_Ctx*)user;
    atomic_store(&w->reactor, r);
    atomic_store(&w->ready, 1);
    return true;
}

static int wake_loop_thread(void* user)
{
    Wake_Ctx* w = (Wake_Ctx*)user;
    mel_reactor_spawn(MEL_REACTOR_THREADED, wake_init, w);
    return 0;
}

MEL_TEST(reactor_defer, cross_thread_defer_wakes_blocked_loop)
{
    Wake_Ctx w = { 0 };
    mel_task_init(&w.task, wake_task_run);

    Mel_Thread th = { 0 };
    bool       spawned = mel_thread_spawn(&th, wake_loop_thread, &w);
    MEL_REQUIRE(spawned);

    while (atomic_load(&w.ready) == 0)
        mel_thread_yield();
    mel_thread_sleep((i64)20 * 1000 * 1000);

    Mel_Reactor* r = (Mel_Reactor*)atomic_load(&w.reactor);
    MEL_REQUIRE_NOT_NULL(r);
    mel_reactor_defer(r, &w.task);

    int code = 0;
    mel_thread_join(&th, &code);

    MEL_EXPECT_EQ(atomic_load(&w.ran), 1);
}

typedef struct
{
    _Atomic(Mel_Reactor*) reactor;
    _Atomic(int)          ready;
    _Atomic(i64)          turns;
    Mel_Task              quit_task;
} Idle_Ctx;

static void idle_quit_run(Mel_Task* self)
{
    Idle_Ctx* it = mel_container_of(self, Idle_Ctx, quit_task);
    mel_reactor_quit((Mel_Reactor*)atomic_load(&it->reactor));
}

static bool idle_count_timer(void* user)
{
    Idle_Ctx* it = (Idle_Ctx*)user;
    atomic_fetch_add_explicit(&it->turns, 1, memory_order_relaxed);
    return true;
}

static bool idle_init(Mel_Reactor* r, void* user)
{
    Idle_Ctx* it = (Idle_Ctx*)user;
    atomic_store(&it->reactor, r);
    Mel_Reactor_Source* timer = mel_reactor_timer_new((i64)1000 * 1000 * 1000, idle_count_timer, it);
    mel_reactor_source_attach(r, timer);
    atomic_store(&it->ready, 1);
    return true;
}

static int idle_loop_thread(void* user)
{
    Idle_Ctx* it = (Idle_Ctx*)user;
    mel_reactor_spawn(MEL_REACTOR_THREADED, idle_init, it);
    return 0;
}

MEL_TEST(reactor_defer, empty_queue_idle_does_not_busy_spin)
{
    Idle_Ctx it = { 0 };
    mel_task_init(&it.quit_task, idle_quit_run);

    Mel_Thread th = { 0 };
    bool       spawned = mel_thread_spawn(&th, idle_loop_thread, &it);
    MEL_REQUIRE(spawned);

    while (atomic_load(&it.ready) == 0)
        mel_thread_yield();
    mel_thread_sleep((i64)50 * 1000 * 1000);

    i64 turns_while_idle = atomic_load(&it.turns);

    Mel_Reactor* r = (Mel_Reactor*)atomic_load(&it.reactor);
    MEL_REQUIRE_NOT_NULL(r);
    mel_reactor_defer(r, &it.quit_task);

    int code = 0;
    mel_thread_join(&th, &code);

    MEL_EXPECT_LT(turns_while_idle, (i64)1000);
}
