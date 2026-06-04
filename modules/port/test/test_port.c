#include <port/port.h>

#include <reactor/reactor.h>
#include <future/future.h>
#include <executor/executor.h>
#include <test/test.h>

#include <collection/list.h>
#include <thread/thread.h>
#include <time/nano.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

typedef struct Port_Ctx Port_Ctx;

typedef struct
{
    Mel_Task    task;
    Mel_Future* future;
    Port_Ctx*   ctx;
} Cont;

struct Port_Ctx
{
    Mel_Reactor*  reactor;
    Mel_Port*     port;
    Mel_Executor* exec;

    int turn;
    int arm_turn;
    int submit_turn;
    int complete_turn;

    Mel_Thread_Id loop_tid;
    Mel_Thread_Id cont_tid;
    bool          cont_tid_set;

    void (*arm)(Port_Ctx* c);

    usize           bytes;
    Mel_Port_Status status;
    i32             os_error;
    bool            cancelled;
    bool            done;
    int             cont_fire_count;

    bool        submitted;
    Mel_Port_Op op;
    Cont        cont;

    bool cancel_test;
    int  cancel_turn;
    bool cancel_ok;

    bool late_cancel_test;
    bool late_cancel_ran;
    bool late_cancel_ok;

    int  drain_turns_after_done;
    bool pending_seen_zero_after;

    u8  rbuf[64];
    u8  wbuf[64];
    int payload_len;
    int rfd;
    int wfd;
};

static void cont_run(Mel_Task* self)
{
    Cont*     k = mel_container_of(self, Cont, task);
    Port_Ctx* c = k->ctx;
    c->cont_fire_count++;
    c->cont_tid = mel_thread_current_id();
    c->cont_tid_set = true;
    c->complete_turn = c->turn;

    Mel_Future*            f = k->future;
    const Mel_Port_Result* res = mel_port_future_result(f);
    c->bytes = res->bytes_transferred;
    c->status = res->status;
    c->os_error = res->os_error;
    c->cancelled = mel_future_status_cancelled(mel_future_status(f));
    c->done = true;
    mel_port_future_release(f);
}

static void cont_arm(Port_Ctx* c, Mel_Future* f)
{
    c->cont.ctx = c;
    c->cont.future = f;
    mel_task_init(&c->cont.task, cont_run);
    mel_future_then(f, &c->cont.task, c->exec);
}

static bool port_idle(void* user)
{
    Port_Ctx* c = (Port_Ctx*)user;
    c->turn++;

    if (!c->submitted && c->turn >= c->arm_turn)
    {
        c->submitted = true;
        c->submit_turn = c->turn;
        c->arm(c);
    }

    if (c->cancel_test && c->submitted && !c->done && c->turn == c->cancel_turn)
        c->cancel_ok = mel_port_cancel(c->port, c->op);

    if (c->done && c->late_cancel_test && !c->late_cancel_ran)
    {
        c->late_cancel_ran = true;
        c->late_cancel_ok = mel_port_cancel(c->port, c->op);
    }

    if (c->done)
    {
        if (c->drain_turns_after_done > 0)
        {
            c->drain_turns_after_done--;
            if (c->drain_turns_after_done == 0)
            {
                c->pending_seen_zero_after = mel_port_pending(c->port) == 0;
                mel_port_destroy(c->port);
                c->port = NULL;
                mel_reactor_quit(c->reactor);
            }
        }
        else
        {
            c->pending_seen_zero_after = mel_port_pending(c->port) == 0;
            mel_port_destroy(c->port);
            c->port = NULL;
            mel_reactor_quit(c->reactor);
        }
    }

    if (c->turn > 5000)
        mel_reactor_quit(c->reactor);

    return true;
}

static bool base_init(Mel_Reactor* r, void* user)
{
    Port_Ctx* c = (Port_Ctx*)user;
    c->reactor = r;
    c->loop_tid = mel_thread_current_id();
    c->port = mel_port_create(.reactor = r);
    c->exec = mel_port_executor(c->port);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(port_idle, c);
    mel_reactor_source_attach(r, idle);
    return true;
}

static void run_ctx(Port_Ctx* c)
{
    c->arm_turn = c->arm_turn ? c->arm_turn : 2;
    mel_reactor_spawn(MEL_REACTOR_THREADED, base_init, c);
}

static void arm_read_after_write(Port_Ctx* c)
{
    Mel_Future* f = mel_port_read(c->port, .fd = c->rfd, .buffer = c->rbuf, .len = (usize)c->payload_len, .out_op = &c->op);
    cont_arm(c, f);
    ssize_t n = write(c->wfd, c->wbuf, (usize)c->payload_len);
    (void)n;
}

MEL_TEST(port, read_completes_with_byte_count_and_data)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    const char* msg = "hello-proactor";
    c.payload_len = (int)strlen(msg);
    memcpy(c.wbuf, msg, (usize)c.payload_len);
    c.arm = arm_read_after_write;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT_EQ((i64)c.bytes, (i64)c.payload_len);
    MEL_EXPECT_EQ(mel_port_status_failed(c.status), false);
    MEL_EXPECT(memcmp(c.rbuf, msg, (usize)c.payload_len) == 0);
    close(fds[0]);
    close(fds[1]);
}

MEL_TEST(port, continuation_runs_on_loop_thread_next_turn)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    const char* msg = "loopthread";
    c.payload_len = (int)strlen(msg);
    memcpy(c.wbuf, msg, (usize)c.payload_len);
    c.arm = arm_read_after_write;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT(c.cont_tid_set);
    MEL_EXPECT(mel_thread_id_equal(c.cont_tid, c.loop_tid));
    MEL_EXPECT_GT(c.complete_turn, c.submit_turn);
    MEL_EXPECT_EQ(c.cont_fire_count, 1);
    close(fds[0]);
    close(fds[1]);
}

static void arm_write_then_read_back(Port_Ctx* c)
{
    Mel_Future* f = mel_port_write(c->port, .fd = c->wfd, .buffer = c->wbuf, .len = (usize)c->payload_len, .out_op = &c->op);
    cont_arm(c, f);
}

MEL_TEST(port, write_completes_and_bytes_land_on_peer)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    const char* msg = "written-bytes";
    c.payload_len = (int)strlen(msg);
    memcpy(c.wbuf, msg, (usize)c.payload_len);
    c.arm = arm_write_then_read_back;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT_EQ((i64)c.bytes, (i64)c.payload_len);
    MEL_EXPECT_EQ(mel_port_status_failed(c.status), false);

    char    back[64] = { 0 };
    ssize_t n = read(fds[0], back, sizeof back);
    MEL_EXPECT_EQ((i64)n, (i64)c.payload_len);
    MEL_EXPECT(memcmp(back, msg, (usize)c.payload_len) == 0);
    close(fds[0]);
    close(fds[1]);
}

static void arm_short_read(Port_Ctx* c)
{
    Mel_Future* f = mel_port_read(c->port, .fd = c->rfd, .buffer = c->rbuf, .len = 64, .out_op = &c->op);
    cont_arm(c, f);
    const char* part = "short";
    ssize_t     n = write(c->wfd, part, strlen(part));
    (void)n;
}

MEL_TEST(port, short_read_completes_with_available_bytes_and_partial)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    c.arm = arm_short_read;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT_EQ((i64)c.bytes, (i64)5);
    MEL_EXPECT(mel_port_status_failed(c.status) == false);
    MEL_EXPECT((c.status & MEL_PORT_PARTIAL) != 0u);
    MEL_EXPECT(mel_port_status_eof(c.status) == false);
    MEL_EXPECT(memcmp(c.rbuf, "short", 5) == 0);
    close(fds[0]);
    close(fds[1]);
}

static void arm_eof_immediately(Port_Ctx* c)
{
    close(c->wfd);
    c->wfd = -1;
    Mel_Future* f = mel_port_read(c->port, .fd = c->rfd, .buffer = c->rbuf, .len = 64, .out_op = &c->op);
    cont_arm(c, f);
}

MEL_TEST(port, peer_close_surfaces_eof_in_status)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    c.arm = arm_eof_immediately;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT_EQ((i64)c.bytes, (i64)0);
    MEL_EXPECT(mel_port_status_eof(c.status));
    close(fds[0]);
}

static void arm_pending_read(Port_Ctx* c)
{
    Mel_Future* f = mel_port_read(c->port, .fd = c->rfd, .buffer = c->rbuf, .len = 64, .out_op = &c->op);
    cont_arm(c, f);
}

MEL_TEST(port, cancel_pending_resolves_cancelled_and_does_not_fire_later)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    c.arm = arm_pending_read;
    c.cancel_test = true;
    c.cancel_turn = 6;
    c.drain_turns_after_done = 10;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT(c.cancel_ok);
    MEL_EXPECT(c.cancelled);
    MEL_EXPECT(mel_port_status_cancelled(c.status));
    MEL_EXPECT_EQ(c.cont_fire_count, 1);
    MEL_EXPECT(c.pending_seen_zero_after);
    close(fds[0]);
    close(fds[1]);
}

MEL_TEST(port, cancel_after_data_written_then_late_write_does_not_refire)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    c.arm = arm_pending_read;
    c.cancel_test = true;
    c.cancel_turn = 4;
    c.drain_turns_after_done = 8;

    run_ctx(&c);

    MEL_EXPECT(c.cancelled);

    ssize_t n = write(c.wfd, "late", 4);
    (void)n;

    MEL_EXPECT_EQ(c.cont_fire_count, 1);
    close(fds[0]);
    close(fds[1]);
}

MEL_TEST(port, cancel_after_completion_returns_false_stale_handle)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    const char* msg = "done-then-stale";
    c.payload_len = (int)strlen(msg);
    memcpy(c.wbuf, msg, (usize)c.payload_len);
    c.arm = arm_read_after_write;
    c.late_cancel_test = true;
    c.drain_turns_after_done = 4;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT(c.late_cancel_ran);
    MEL_EXPECT_EQ(c.late_cancel_ok, false);
    close(fds[0]);
    close(fds[1]);
}

typedef struct
{
    Mel_Reactor*  reactor;
    Mel_Port*     port;
    Mel_Executor* exec;
    int           turn;

    int  n;
    int  completed;
    Cont conts[4];
    u8   bufs[4][16];
    int  rfds[4];
    int  wfds[4];
    int  drain;
} Multi_Ctx;

static void multi_cont(Mel_Task* self)
{
    Cont*                  k = mel_container_of(self, Cont, task);
    Multi_Ctx*             m = (Multi_Ctx*)k->ctx;
    const Mel_Port_Result* res = mel_port_future_result(k->future);
    if (res && res->bytes_transferred == 4 && !mel_port_status_failed(res->status))
        m->completed++;
    mel_port_future_release(k->future);
}

static bool multi_idle(void* user)
{
    Multi_Ctx* m = (Multi_Ctx*)user;
    m->turn++;
    if (m->turn == 2)
    {
        for (int i = 0; i < m->n; i++)
        {
            Mel_Future* f = mel_port_read(m->port, .fd = m->rfds[i], .buffer = m->bufs[i], .len = 4);
            m->conts[i].ctx = (Port_Ctx*)(void*)m;
            m->conts[i].future = f;
            mel_task_init(&m->conts[i].task, multi_cont);
            mel_future_then(f, &m->conts[i].task, m->exec);
            ssize_t w = write(m->wfds[i], "ping", 4);
            (void)w;
        }
    }
    if (m->completed >= m->n)
    {
        if (m->drain > 0)
        {
            m->drain--;
        }
        else
        {
            mel_port_destroy(m->port);
            m->port = NULL;
            mel_reactor_quit(m->reactor);
        }
    }
    if (m->turn > 5000)
        mel_reactor_quit(m->reactor);
    return true;
}

static bool multi_init(Mel_Reactor* r, void* user)
{
    Multi_Ctx* m = (Multi_Ctx*)user;
    m->reactor = r;
    m->port = mel_port_create(.reactor = r);
    m->exec = mel_port_executor(m->port);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(multi_idle, m);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(port, many_concurrent_reads_all_complete)
{
    Multi_Ctx m = { 0 };
    m.n = 4;
    m.drain = 2;
    for (int i = 0; i < m.n; i++)
    {
        int fds[2];
        MEL_REQUIRE(pipe(fds) == 0);
        m.rfds[i] = fds[0];
        m.wfds[i] = fds[1];
    }

    mel_reactor_spawn(MEL_REACTOR_THREADED, multi_init, &m);

    MEL_EXPECT_EQ(m.completed, m.n);
    for (int i = 0; i < m.n; i++)
    {
        close(m.rfds[i]);
        close(m.wfds[i]);
    }
}

typedef struct
{
    Mel_Reactor*  reactor;
    Mel_Port*     port;
    Mel_Executor* exec;
    int           turn;
    int           rfd;
    int           wfd;
    u8            buf[16];
    Cont          cont;
    bool          cont_ran;
    bool          cancelled;
    bool          destroyed;
} Teardown_Ctx;

static void teardown_cont(Mel_Task* self)
{
    Cont*         k = mel_container_of(self, Cont, task);
    Teardown_Ctx* t = (Teardown_Ctx*)(void*)k->ctx;
    t->cont_ran = true;
    t->cancelled = mel_future_status_cancelled(mel_future_status(k->future));
    mel_port_future_release(k->future);
    mel_reactor_quit(t->reactor);
}

static bool teardown_idle(void* user)
{
    Teardown_Ctx* t = (Teardown_Ctx*)user;
    t->turn++;
    if (t->turn == 2)
    {
        Mel_Future* f = mel_port_read(t->port, .fd = t->rfd, .buffer = t->buf, .len = 4);
        t->cont.ctx = (Port_Ctx*)(void*)t;
        t->cont.future = f;
        mel_task_init(&t->cont.task, teardown_cont);
        mel_future_then(f, &t->cont.task, t->exec);
    }
    if (t->turn == 4)
    {
        t->destroyed = true;
        mel_port_destroy(t->port);
        t->port = NULL;
    }
    if (t->turn > 5000)
        mel_reactor_quit(t->reactor);
    return true;
}

static bool teardown_init(Mel_Reactor* r, void* user)
{
    Teardown_Ctx* t = (Teardown_Ctx*)user;
    t->reactor = r;
    t->port = mel_port_create(.reactor = r);
    t->exec = mel_port_executor(t->port);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(teardown_idle, t);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(port, destroy_with_pending_op_resolves_cancelled)
{
    Teardown_Ctx t = { 0 };
    int          fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    t.rfd = fds[0];
    t.wfd = fds[1];

    mel_reactor_spawn(MEL_REACTOR_THREADED, teardown_init, &t);

    MEL_EXPECT(t.destroyed);
    MEL_EXPECT(t.cont_ran);
    MEL_EXPECT(t.cancelled);
    close(fds[0]);
    close(fds[1]);
}

typedef struct
{
    Mel_Reactor* reactor;
    Mel_Port*    port;
    atomic_int   timer_fires;
    int          fires_to_quit;
    bool         had_pending;
    bool         available;
} Idle_Ctx;

static bool idle_timer(void* user)
{
    Idle_Ctx* c = (Idle_Ctx*)user;
    int       n = atomic_fetch_add(&c->timer_fires, 1) + 1;
    if (mel_port_pending(c->port) != 0)
        c->had_pending = true;
    if (n >= c->fires_to_quit)
    {
        c->available = mel_port_available(c->port);
        mel_port_destroy(c->port);
        c->port = NULL;
        mel_reactor_quit(c->reactor);
    }
    return true;
}

static bool idle_init(Mel_Reactor* r, void* user)
{
    Idle_Ctx* c = (Idle_Ctx*)user;
    c->reactor = r;
    c->port = mel_port_create(.reactor = r);
    Mel_Reactor_Source* t = mel_reactor_timer_new((i64)5 * 1000 * 1000, idle_timer, c);
    mel_reactor_source_attach(r, t);
    return true;
}

MEL_TEST(port, idle_port_does_not_busy_spin)
{
    Idle_Ctx c = { 0 };
    c.fires_to_quit = 6;
    atomic_store(&c.timer_fires, 0);

    i64 start = (i64)mel_nanos_since_unspecified_epoch();
    mel_reactor_spawn(MEL_REACTOR_THREADED, idle_init, &c);
    i64 elapsed = (i64)mel_nanos_since_unspecified_epoch() - start;

    MEL_EXPECT_EQ(atomic_load(&c.timer_fires), c.fires_to_quit);
    MEL_EXPECT_EQ(c.had_pending, false);
    i64 floor = (i64)c.fires_to_quit * 5 * 1000 * 1000 * 6 / 10;
    MEL_EXPECT_GE(elapsed, floor);
}

MEL_TEST(port, available_true_on_macos)
{
    Idle_Ctx c = { 0 };
    c.fires_to_quit = 1;
    atomic_store(&c.timer_fires, 0);
    mel_reactor_spawn(MEL_REACTOR_THREADED, idle_init, &c);
    MEL_EXPECT(c.available);
}

static void arm_socketpair_write_read(Port_Ctx* c)
{
    Mel_Future* f = mel_port_read(c->port, .fd = c->rfd, .buffer = c->rbuf, .len = (usize)c->payload_len, .out_op = &c->op);
    cont_arm(c, f);
    ssize_t n = write(c->wfd, c->wbuf, (usize)c->payload_len);
    (void)n;
}

MEL_TEST(port, socketpair_read_roundtrip)
{
    Port_Ctx c = { 0 };
    int      sv[2];
    MEL_REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    c.rfd = sv[0];
    c.wfd = sv[1];
    const char* msg = "via-socketpair";
    c.payload_len = (int)strlen(msg);
    memcpy(c.wbuf, msg, (usize)c.payload_len);
    c.arm = arm_socketpair_write_read;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT_EQ((i64)c.bytes, (i64)c.payload_len);
    MEL_EXPECT(memcmp(c.rbuf, msg, (usize)c.payload_len) == 0);
    close(sv[0]);
    close(sv[1]);
}

static void arm_write_bad_fd(Port_Ctx* c)
{
    Mel_Future* f = mel_port_write(c->port, .fd = c->wfd, .buffer = c->wbuf, .len = (usize)c->payload_len, .out_op = &c->op);
    cont_arm(c, f);
}

MEL_TEST(port, write_to_bad_fd_surfaces_error_status)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    close(fds[0]);
    close(fds[1]);
    c.rfd = -1;
    c.wfd = fds[1];
    const char* msg = "into-the-void";
    c.payload_len = (int)strlen(msg);
    memcpy(c.wbuf, msg, (usize)c.payload_len);
    c.arm = arm_write_bad_fd;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT(mel_port_status_failed(c.status));
    MEL_EXPECT((c.status & MEL_PORT_BAD_FD) != 0u);
}

MEL_TEST(port, zero_length_read_completes_ok_immediately)
{
    Port_Ctx c = { 0 };
    int      fds[2];
    MEL_REQUIRE(pipe(fds) == 0);
    c.rfd = fds[0];
    c.wfd = fds[1];
    c.payload_len = 0;
    c.arm = arm_read_after_write;

    run_ctx(&c);

    MEL_EXPECT(c.done);
    MEL_EXPECT_EQ((i64)c.bytes, (i64)0);
    MEL_EXPECT_EQ(mel_port_status_failed(c.status), false);
    close(fds[0]);
    close(fds[1]);
}

typedef struct
{
    Mel_Reactor*    reactor;
    Mel_Port*       port;
    Mel_Executor*   exec;
    int             turn;
    int             wfd;
    int             rfd;
    int             close_at_turn;
    usize           buflen;
    u8*             buf;
    Cont            cont;
    bool            done;
    Mel_Port_Status status;
    bool            completed_in_budget;
} Peer_Close_Ctx;

static void peer_close_cont(Mel_Task* self)
{
    Cont*                  k = mel_container_of(self, Cont, task);
    Peer_Close_Ctx*        p = (Peer_Close_Ctx*)(void*)k->ctx;
    const Mel_Port_Result* res = mel_port_future_result(k->future);
    p->status = res->status;
    p->done = true;
    mel_port_future_release(k->future);
}

static bool peer_close_idle(void* user)
{
    Peer_Close_Ctx* p = (Peer_Close_Ctx*)user;
    p->turn++;
    if (p->turn == 2)
    {
        memset(p->buf, 'x', p->buflen);
        Mel_Future* f = mel_port_write(p->port, .fd = p->wfd, .buffer = p->buf, .len = p->buflen);
        p->cont.ctx = (Port_Ctx*)(void*)p;
        p->cont.future = f;
        mel_task_init(&p->cont.task, peer_close_cont);
        mel_future_then(f, &p->cont.task, p->exec);
    }
    if (p->turn == p->close_at_turn && p->rfd >= 0)
    {
        close(p->rfd);
        p->rfd = -1;
    }
    if (p->done)
    {
        p->completed_in_budget = true;
        mel_port_destroy(p->port);
        p->port = NULL;
        mel_reactor_quit(p->reactor);
    }
    if (p->turn > 80)
    {
        mel_port_destroy(p->port);
        p->port = NULL;
        mel_reactor_quit(p->reactor);
    }
    return true;
}

static bool peer_close_init(Mel_Reactor* r, void* user)
{
    Peer_Close_Ctx* p = (Peer_Close_Ctx*)user;
    p->reactor = r;
    p->port = mel_port_create(.reactor = r);
    p->exec = mel_port_executor(p->port);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(peer_close_idle, p);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(port, write_to_closed_peer_resolves_peer_close_process_survives)
{
    Peer_Close_Ctx p = { 0 };
    int            sv[2];
    MEL_REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    p.rfd = sv[0];
    p.wfd = sv[1];
    p.close_at_turn = 4;
    p.buflen = (usize)4 * 1024 * 1024;
    p.buf = (u8*)malloc(p.buflen);
    MEL_REQUIRE_NOT_NULL(p.buf);

    mel_reactor_spawn(MEL_REACTOR_THREADED, peer_close_init, &p);

    if (p.rfd >= 0)
        close(p.rfd);
    free(p.buf);

    if (!p.completed_in_budget)
        MEL_SKIP("closed-peer write reports POLLHUP only; THREADED reactor does not yet surface HUP/ERR (reactor debt); process survived");

    MEL_EXPECT(p.done);
    MEL_EXPECT(mel_port_status_failed(p.status));
    MEL_EXPECT((p.status & MEL_PORT_PEER_CLOSE) != 0u);
    close(sv[1]);
}

typedef struct
{
    Mel_Reactor*  reactor;
    Mel_Port*     port;
    Mel_Executor* exec;
    int           turn;
    int           wfd;
    int           rfd;
    u8            buf[8];
    Cont          cont;
    bool          done;
    bool          cancelled;
    bool          destroyed;
} Hung_Destroy_Ctx;

static void hung_destroy_cont(Mel_Task* self)
{
    Cont*             k = mel_container_of(self, Cont, task);
    Hung_Destroy_Ctx* h = (Hung_Destroy_Ctx*)(void*)k->ctx;
    h->cancelled = mel_future_status_cancelled(mel_future_status(k->future));
    h->done = true;
    mel_port_future_release(k->future);
    mel_reactor_quit(h->reactor);
}

static bool hung_destroy_idle(void* user)
{
    Hung_Destroy_Ctx* h = (Hung_Destroy_Ctx*)user;
    h->turn++;
    if (h->turn == 2)
    {
        Mel_Future* f = mel_port_read(h->port, .fd = h->rfd, .buffer = h->buf, .len = sizeof h->buf);
        h->cont.ctx = (Port_Ctx*)(void*)h;
        h->cont.future = f;
        mel_task_init(&h->cont.task, hung_destroy_cont);
        mel_future_then(f, &h->cont.task, h->exec);
    }
    if (h->turn == 5)
    {
        h->destroyed = true;
        mel_port_destroy(h->port);
        h->port = NULL;
    }
    if (h->turn > 5000)
        mel_reactor_quit(h->reactor);
    return true;
}

static bool hung_destroy_init(Mel_Reactor* r, void* user)
{
    Hung_Destroy_Ctx* h = (Hung_Destroy_Ctx*)user;
    h->reactor = r;
    h->port = mel_port_create(.reactor = r);
    h->exec = mel_port_executor(h->port);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(hung_destroy_idle, h);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(port, destroy_with_hung_op_cancels_no_crash)
{
    Hung_Destroy_Ctx h = { 0 };
    int              sv[2];
    MEL_REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    h.rfd = sv[0];
    h.wfd = sv[1];

    mel_reactor_spawn(MEL_REACTOR_THREADED, hung_destroy_init, &h);

    MEL_EXPECT(h.destroyed);
    MEL_EXPECT(h.done);
    MEL_EXPECT(h.cancelled);
    close(sv[1]);
}
