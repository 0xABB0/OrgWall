#include <reactor/reactor.h>
#include <test/test.h>

#include <thread/thread.h>
#include <time/nano.h>

#include <fcntl.h>
#include <poll.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct
{
    Mel_Reactor_Source base;
    Mel_Reactor_Poll   poll;
    void*              ctx;
} Watch_Source;

typedef struct
{
    Mel_Reactor* reactor;

    int local_fd;
    int peer_fd;

    u32 want_events;

    _Atomic(int) ready;
    _Atomic(int) dispatched;
    u32          seen_revents;
    int          eof_bytes;
    bool         eof_seen;

    Watch_Source* watch;
} Hup_Ctx;

static bool watch_check(Mel_Reactor_Source* s)
{
    Watch_Source* w = (Watch_Source*)s;
    u32           re = w->poll.revents;
    return (re & (w->poll.events | MEL_REACTOR_POLL_HUP | MEL_REACTOR_POLL_ERR)) != 0;
}

static bool watch_dispatch(Mel_Reactor_Source* s, Mel_Reactor_Source_Proc cb, void* user)
{
    (void)cb;
    Watch_Source* w = (Watch_Source*)s;
    Hup_Ctx*      c = (Hup_Ctx*)w->ctx;

    c->seen_revents = w->poll.revents;

    if (w->poll.revents & MEL_REACTOR_POLL_IN)
    {
        char    buf[256];
        ssize_t n = read(c->local_fd, buf, sizeof buf);
        if (n == 0)
        {
            c->eof_seen = true;
            c->eof_bytes = 0;
        }
    }

    atomic_store(&c->dispatched, 1);
    mel_reactor_quit(c->reactor);
    (void)user;
    return true;
}

static const Mel_Reactor_Source_Callbacks g_watch_cb = {
    .check = watch_check,
    .dispatch = watch_dispatch,
};

static bool hup_init(Mel_Reactor* r, void* user)
{
    Hup_Ctx* c = (Hup_Ctx*)user;
    c->reactor = r;

    Watch_Source* w = (Watch_Source*)mel_reactor_source_new(&g_watch_cb, sizeof(Watch_Source));
    w->ctx = c;
    w->poll.handle = c->local_fd;
    w->poll.events = c->want_events;
    w->poll.revents = 0;
    c->watch = w;

    mel_reactor_source_add_poll(&w->base, &w->poll);
    mel_reactor_source_attach(r, &w->base);

    atomic_store(&c->ready, 1);
    return true;
}

static int hup_loop_thread(void* user)
{
    Hup_Ctx* c = (Hup_Ctx*)user;
    mel_reactor_spawn(MEL_REACTOR_THREADED, hup_init, c);
    return 0;
}

static void fill_send_buffer(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    char buf[65536];
    memset(buf, 'x', sizeof buf);
    for (;;)
    {
        ssize_t n = write(fd, buf, sizeof buf);
        if (n <= 0)
            break;
    }
    fcntl(fd, F_SETFL, flags);
}

MEL_TEST(reactor_hup, write_watched_peer_close_dispatches_with_hup)
{
    int sv[2];
    MEL_REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    Hup_Ctx c = { 0 };
    c.local_fd = sv[0];
    c.peer_fd = sv[1];
    c.want_events = MEL_REACTOR_POLL_OUT;

    fill_send_buffer(c.local_fd);

    Mel_Thread th = { 0 };
    MEL_REQUIRE(mel_thread_spawn(&th, hup_loop_thread, &c));

    while (atomic_load(&c.ready) == 0)
        mel_thread_yield();
    mel_thread_sleep((i64)20 * 1000 * 1000);

    MEL_REQUIRE_EQ(atomic_load(&c.dispatched), 0);

    close(c.peer_fd);
    c.peer_fd = -1;

    int code = 0;
    mel_thread_join(&th, &code);

    MEL_EXPECT_EQ(atomic_load(&c.dispatched), 1);
    MEL_EXPECT((c.seen_revents & MEL_REACTOR_POLL_HUP) != 0);

    close(c.local_fd);
}

MEL_TEST(reactor_hup, write_watched_peer_closed_before_attach_still_dispatches_hup)
{
    int sv[2];
    MEL_REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    Hup_Ctx c = { 0 };
    c.local_fd = sv[0];
    c.peer_fd = sv[1];
    c.want_events = MEL_REACTOR_POLL_OUT;

    fill_send_buffer(c.local_fd);
    close(c.peer_fd);
    c.peer_fd = -1;
    mel_thread_sleep((i64)10 * 1000 * 1000);

    Mel_Thread th = { 0 };
    MEL_REQUIRE(mel_thread_spawn(&th, hup_loop_thread, &c));

    int code = 0;
    mel_thread_join(&th, &code);

    MEL_EXPECT_EQ(atomic_load(&c.dispatched), 1);
    MEL_EXPECT((c.seen_revents & MEL_REACTOR_POLL_HUP) != 0);

    close(c.local_fd);
}

MEL_TEST(reactor_hup, read_watched_peer_close_surfaces_eof_in)
{
    int sv[2];
    MEL_REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    Hup_Ctx c = { 0 };
    c.local_fd = sv[0];
    c.peer_fd = sv[1];
    c.want_events = MEL_REACTOR_POLL_IN;

    Mel_Thread th = { 0 };
    MEL_REQUIRE(mel_thread_spawn(&th, hup_loop_thread, &c));

    while (atomic_load(&c.ready) == 0)
        mel_thread_yield();
    mel_thread_sleep((i64)20 * 1000 * 1000);

    MEL_REQUIRE_EQ(atomic_load(&c.dispatched), 0);

    close(c.peer_fd);
    c.peer_fd = -1;

    int code = 0;
    mel_thread_join(&th, &code);

    MEL_EXPECT_EQ(atomic_load(&c.dispatched), 1);
    MEL_EXPECT((c.seen_revents & MEL_REACTOR_POLL_IN) != 0);
    MEL_EXPECT(c.eof_seen);
    MEL_EXPECT_EQ(c.eof_bytes, 0);
}
