#include <net/net.h>
#include <net/address.h>
#include <net/resolve.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <allocator/heap.h>
#include <collection/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <io/stream.h>
#include <string/str8.h>
#include <test/test.h>
#include <vat/vat.h>

#include <string.h>

static str8 fmt_addr(const Mel_Net_Address* a) { return mel_net_address_format(a, mel_alloc_heap()); }

static void free_str(str8 s)
{
    if (s.data)
        mel_dealloc(mel_alloc_heap(), s.data);
}

MEL_TEST(net, address_v4_parse_format_roundtrip)
{
    Mel_Net_Address a;
    MEL_REQUIRE(mel_net_status_ok(mel_net_address_parse(S8("192.168.1.10"), 8080, &a)));
    MEL_EXPECT(!a.v6);
    MEL_EXPECT_EQ(a.port, 8080);
    MEL_EXPECT_EQ(a.bytes[0], 192);
    MEL_EXPECT_EQ(a.bytes[3], 10);
    str8 s = fmt_addr(&a);
    MEL_EXPECT_EQ_STR8(s, S8("192.168.1.10"));
    free_str(s);
}

MEL_TEST(net, address_v4_rejects_malformed)
{
    Mel_Net_Address a;
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("256.1.1.1"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("01.2.3.4"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("1.2.3"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("1.2.3.4.5"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8(""), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("hello"), 0, &a)));
}

MEL_TEST(net, address_v6_loopback_roundtrip)
{
    Mel_Net_Address a;
    MEL_REQUIRE(mel_net_status_ok(mel_net_address_parse(S8("::1"), 443, &a)));
    MEL_EXPECT(a.v6);
    MEL_EXPECT(mel_net_address_is_loopback(&a));
    Mel_Net_Address ref = mel_net_address_v6_loopback(443);
    MEL_EXPECT(mel_net_address_equals(&a, &ref));
    str8 s = fmt_addr(&a);
    MEL_EXPECT_EQ_STR8(s, S8("::1"));
    free_str(s);
}

MEL_TEST(net, address_v6_compression_roundtrip)
{
    Mel_Net_Address a;
    MEL_REQUIRE(mel_net_status_ok(mel_net_address_parse(S8("2001:db8::ff00:42:8329"), 0, &a)));
    str8 s = fmt_addr(&a);
    MEL_EXPECT_EQ_STR8(s, S8("2001:db8::ff00:42:8329"));
    free_str(s);

    MEL_REQUIRE(mel_net_status_ok(mel_net_address_parse(S8("2001:db8:0:0:1:0:0:1"), 0, &a)));
    str8 t = fmt_addr(&a);
    MEL_EXPECT_EQ_STR8(t, S8("2001:db8::1:0:0:1"));
    free_str(t);
}

MEL_TEST(net, address_v6_brackets_and_mapped)
{
    Mel_Net_Address a;
    MEL_REQUIRE(mel_net_status_ok(mel_net_address_parse(S8("[::1]"), 80, &a)));
    MEL_EXPECT(mel_net_address_is_loopback(&a));

    MEL_REQUIRE(mel_net_status_ok(mel_net_address_parse(S8("::ffff:192.168.1.1"), 0, &a)));
    MEL_EXPECT(mel_net_address_is_v4_mapped(&a));
    str8 s = fmt_addr(&a);
    MEL_EXPECT_EQ_STR8(s, S8("::ffff:192.168.1.1"));
    free_str(s);
}

MEL_TEST(net, address_v6_rejects_malformed)
{
    Mel_Net_Address a;
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8(":::"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("1::2::3"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("1:2:3:4:5:6:7"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("1:2:3:4:5:6:7:8:9"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("12345::"), 0, &a)));
    MEL_EXPECT(mel_net_status_failed(mel_net_address_parse(S8("g::1"), 0, &a)));
}

typedef struct Net_Ctx Net_Ctx;

struct Net_Ctx
{
    Mel_Vat*      vat;
    Mel_Net*      net;
    Mel_Executor* exec;
    int           turn;
    bool          finished;
    void (*step)(Net_Ctx* c);

    Mel_Net_Listener* listener;
    Mel_Net_Conn*     client;
    Mel_Net_Conn*     server;
    Mel_Net_Status    connect_status;
    Mel_Net_Status    accept_status;
    bool              connect_done;
    bool              accept_done;

    bool          started;
    bool          ping_written;
    bool          ping_read;
    bool          pong_written;
    bool          pong_read;
    u8            srv_buf[16];
    u8            cli_buf[16];
    Mel_IO_Result srv_read;
    Mel_IO_Result cli_read;

    Mel_Net_Udp*       udp_a;
    Mel_Net_Udp*       udp_b;
    bool               udp_started;
    bool               udp_recv_done;
    bool               udp_send_done;
    Mel_Net_Udp_Result udp_recv;
    u8                 udp_buf[32];

    bool                   resolve_started;
    bool                   resolve_done;
    Mel_Net_Resolve_Result resolve_res;
    Mel_Net_Address        resolve_first;

    Mel_Net_Op op;
    bool       cancel_requested_flag;
    bool       cancel_ok;
    bool       cancelled_seen;
};

typedef struct
{
    Mel_Task    task;
    Mel_Future* future;
    Net_Ctx*    ctx;
    void (*fn)(Net_Ctx* c, Mel_Future* f);
} Net_Cont;

static void net_cont_run(Mel_Task* self)
{
    Net_Cont* k = mel_container_of(self, Net_Cont, task);
    k->fn(k->ctx, k->future);
}

static void net_cont_arm(Net_Ctx* c, Net_Cont* k, Mel_Future* f, void (*fn)(Net_Ctx*, Mel_Future*))
{
    k->ctx = c;
    k->future = f;
    k->fn = fn;
    mel_task_init(&k->task, net_cont_run);
    mel_future_then(f, &k->task, c->exec);
}

static bool net_idle(void* user)
{
    Net_Ctx* c = (Net_Ctx*)user;
    c->turn++;
    c->step(c);
    if (c->finished)
    {
        if (c->net)
        {
            mel_net_destroy(c->net);
            c->net = NULL;
        }
        mel_vat_quit(c->vat);
    }
    if (c->turn > 5000)
        mel_vat_quit(c->vat);
    return true;
}

static i64 net_idle_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return 0;
}

static bool net_idle_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    net_idle(mel_vat_source_state(s));
    return false;
}

static const Mel_Vat_Source_Vtbl NET_IDLE_VT = {
    .wakeables = NULL,
    .deadline = net_idle_deadline,
    .drain = net_idle_drain,
    .cancel = NULL,
};

static void net_run_ctx(Net_Ctx* c)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Vat_Waiter*  waiter = mel_vat_waiter_io(a);
    Mel_Vat_Driver*  driver = mel_vat_driver_fair(a, 64);
    Mel_Vat*         vat = mel_vat_open(a, (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    c->vat = vat;
    c->net = mel_net_create(.vat = vat, .resolver_workers = 1);
    c->exec = mel_net_executor(c->net);
    Mel_Vat_Source* idle = mel_vat_source_open(vat, &NET_IDLE_VT, c);
    mel_vat_run(vat);
    mel_vat_source_close(idle);
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
}

static Net_Cont g_conts[8];

static void echo_on_connect(Net_Ctx* c, Mel_Future* f)
{
    const Mel_Net_Conn_Result* r = mel_net_future_conn(f);
    c->connect_status = r->status;
    c->client = mel_net_future_take_conn(f);
    c->connect_done = true;
    mel_net_future_release(f);
}

static void echo_on_accept(Net_Ctx* c, Mel_Future* f)
{
    const Mel_Net_Conn_Result* r = mel_net_future_conn(f);
    c->accept_status = r->status;
    c->server = mel_net_future_take_conn(f);
    c->accept_done = true;
    mel_net_future_release(f);
}

static void echo_on_srv_read(Net_Ctx* c, Mel_Future* f)
{
    c->srv_read = *mel_stream_future_result(f);
    c->ping_read = true;
    mel_stream_future_release(f);
}

static void echo_on_cli_read(Net_Ctx* c, Mel_Future* f)
{
    c->cli_read = *mel_stream_future_result(f);
    c->pong_read = true;
    mel_stream_future_release(f);
}

static void echo_release_io(Net_Ctx* c, Mel_Future* f)
{
    (void)c;
    mel_stream_future_release(f);
}

static void echo_step(Net_Ctx* c)
{
    if (c->turn == 2)
    {
        Mel_Net_Listener_Result lr = mel_net_tcp_listen(c->net, .address = mel_net_address_v4_loopback(0), .backlog = 8, .reuse_addr = true);
        if (!mel_net_status_ok(lr.status))
        {
            c->finished = true;
            return;
        }
        c->listener = lr.value;
        net_cont_arm(c, &g_conts[0], mel_net_listener_accept(c->listener), echo_on_accept);
        net_cont_arm(c, &g_conts[1], mel_net_tcp_connect(c->net, .address = mel_net_listener_address(c->listener), .timeout_ns = (i64)5 * 1000 * 1000 * 1000, .nodelay = true), echo_on_connect);
        return;
    }

    if (c->connect_done && c->accept_done && c->client && c->server && !c->started)
    {
        c->started = true;
        net_cont_arm(c, &g_conts[2], mel_stream_read(mel_net_conn_stream(c->server), .buffer = c->srv_buf, .len = 4), echo_on_srv_read);
        net_cont_arm(c, &g_conts[3], mel_stream_write(mel_net_conn_stream(c->client), .buffer = "ping", .len = 4), echo_release_io);
        c->ping_written = true;
        return;
    }

    if (c->ping_read && !c->pong_written)
    {
        c->pong_written = true;
        net_cont_arm(c, &g_conts[4], mel_stream_read(mel_net_conn_stream(c->client), .buffer = c->cli_buf, .len = 4), echo_on_cli_read);
        net_cont_arm(c, &g_conts[5], mel_stream_write(mel_net_conn_stream(c->server), .buffer = "pong", .len = 4), echo_release_io);
        return;
    }

    if (c->pong_read && !c->finished)
    {
        mel_net_conn_destroy(c->client);
        c->client = NULL;
        mel_net_conn_destroy(c->server);
        c->server = NULL;
        mel_net_listener_destroy(c->listener);
        c->listener = NULL;
        c->finished = true;
    }
}

MEL_TEST(net, tcp_loopback_echo_roundtrip)
{
    Net_Ctx c = { 0 };
    c.step = echo_step;

    net_run_ctx(&c);

    MEL_EXPECT(c.connect_done);
    MEL_EXPECT(c.accept_done);
    MEL_EXPECT(mel_net_status_ok(c.connect_status));
    MEL_EXPECT(mel_net_status_ok(c.accept_status));
    MEL_EXPECT(c.ping_read);
    MEL_EXPECT(c.pong_read);
    MEL_EXPECT_EQ((i64)c.srv_read.bytes_transferred, (i64)4);
    MEL_EXPECT_EQ((i64)c.cli_read.bytes_transferred, (i64)4);
    MEL_EXPECT(memcmp(c.srv_buf, "ping", 4) == 0);
    MEL_EXPECT(memcmp(c.cli_buf, "pong", 4) == 0);
}

static void udp_on_recv(Net_Ctx* c, Mel_Future* f)
{
    c->udp_recv = *mel_net_future_udp(f);
    c->udp_recv_done = true;
    mel_net_future_release(f);
}

static void udp_on_send(Net_Ctx* c, Mel_Future* f)
{
    c->udp_send_done = true;
    mel_net_future_release(f);
}

static void udp_step(Net_Ctx* c)
{
    if (c->turn == 2)
    {
        Mel_Net_Udp_Open_Result a = mel_net_udp_open(c->net, .address = mel_net_address_v4_loopback(0), .bind = true);
        Mel_Net_Udp_Open_Result b = mel_net_udp_open(c->net, .address = mel_net_address_v4_loopback(0), .bind = true);
        if (!mel_net_status_ok(a.status) || !mel_net_status_ok(b.status))
        {
            c->finished = true;
            return;
        }
        c->udp_a = a.value;
        c->udp_b = b.value;
        net_cont_arm(c, &g_conts[0], mel_net_udp_recv(c->udp_b, .buffer = c->udp_buf, .len = sizeof c->udp_buf), udp_on_recv);
        net_cont_arm(c, &g_conts[1], mel_net_udp_send(c->udp_a, .address = mel_net_udp_address(c->udp_b), .buffer = "hello", .len = 5), udp_on_send);
        c->udp_started = true;
        return;
    }

    if (c->udp_started && c->udp_recv_done && c->udp_send_done && !c->finished)
    {
        mel_net_udp_destroy(c->udp_a);
        c->udp_a = NULL;
        mel_net_udp_destroy(c->udp_b);
        c->udp_b = NULL;
        c->finished = true;
    }
}

MEL_TEST(net, udp_loopback_roundtrip)
{
    Net_Ctx c = { 0 };
    c.step = udp_step;

    net_run_ctx(&c);

    MEL_EXPECT(c.udp_recv_done);
    MEL_EXPECT(c.udp_send_done);
    MEL_EXPECT(mel_net_status_ok(c.udp_recv.status));
    MEL_EXPECT_EQ((i64)c.udp_recv.bytes, (i64)5);
    MEL_EXPECT(memcmp(c.udp_buf, "hello", 5) == 0);
    MEL_EXPECT(mel_net_address_is_loopback(&c.udp_recv.from));
}

static void resolve_on_done(Net_Ctx* c, Mel_Future* f)
{
    c->resolve_res = *mel_net_future_resolve(f);
    if (c->resolve_res.count > 0)
        c->resolve_first = c->resolve_res.items[0];
    c->resolve_done = true;
    mel_net_future_release(f);
}

static void resolve_step(Net_Ctx* c)
{
    if (c->turn == 2 && !c->resolve_started)
    {
        c->resolve_started = true;
        net_cont_arm(c, &g_conts[0], mel_net_resolve(c->net, S8("localhost"), .port = 80), resolve_on_done);
        return;
    }
    if (c->resolve_done && !c->finished)
        c->finished = true;
}

MEL_TEST(net, resolve_localhost_is_loopback)
{
    Net_Ctx c = { 0 };
    c.step = resolve_step;

    net_run_ctx(&c);

    MEL_EXPECT(c.resolve_done);
    MEL_REQUIRE(mel_net_status_ok(c.resolve_res.status));
    MEL_REQUIRE_GT((i64)c.resolve_res.count, (i64)0);
    MEL_EXPECT(mel_net_address_is_loopback(&c.resolve_first));
    MEL_EXPECT_EQ(c.resolve_first.port, 80);
}

static void resolve_literal_step(Net_Ctx* c)
{
    if (c->turn == 2 && !c->resolve_started)
    {
        c->resolve_started = true;
        net_cont_arm(c, &g_conts[0], mel_net_resolve(c->net, S8("127.0.0.1"), .port = 7777), resolve_on_done);
        return;
    }
    if (c->resolve_done && !c->finished)
        c->finished = true;
}

MEL_TEST(net, resolve_literal_short_circuits)
{
    Net_Ctx c = { 0 };
    c.step = resolve_literal_step;

    net_run_ctx(&c);

    MEL_EXPECT(c.resolve_done);
    MEL_REQUIRE(mel_net_status_ok(c.resolve_res.status));
    MEL_REQUIRE_EQ((i64)c.resolve_res.count, (i64)1);
    Mel_Net_Address ref = mel_net_address_v4_loopback(7777);
    MEL_EXPECT(mel_net_address_equals(&c.resolve_first, &ref));
}

static void refused_on_connect(Net_Ctx* c, Mel_Future* f)
{
    const Mel_Net_Conn_Result* r = mel_net_future_conn(f);
    c->connect_status = r->status;
    c->client = mel_net_future_take_conn(f);
    c->connect_done = true;
    mel_net_future_release(f);
}

static void refused_step(Net_Ctx* c)
{
    if (c->turn == 2 && !c->started)
    {
        c->started = true;
        net_cont_arm(c, &g_conts[0], mel_net_tcp_connect(c->net, .address = mel_net_address_v4_loopback(1), .timeout_ns = (i64)5 * 1000 * 1000 * 1000), refused_on_connect);
        return;
    }
    if (c->connect_done && !c->finished)
    {
        if (c->client)
        {
            mel_net_conn_destroy(c->client);
            c->client = NULL;
        }
        c->finished = true;
    }
}

MEL_TEST(net, connect_to_closed_port_is_refused)
{
    Net_Ctx c = { 0 };
    c.step = refused_step;

    net_run_ctx(&c);

    MEL_EXPECT(c.connect_done);
    MEL_EXPECT(mel_net_status_failed(c.connect_status));
    MEL_EXPECT((c.connect_status & MEL_NET_REFUSED) != 0u);
    MEL_EXPECT_NULL(c.client);
}

static void cancel_on_accept(Net_Ctx* c, Mel_Future* f)
{
    c->cancelled_seen = mel_future_status_cancelled(mel_future_status(f));
    c->accept_done = true;
    mel_net_future_release(f);
}

static void cancel_step(Net_Ctx* c)
{
    if (c->turn == 2 && !c->started)
    {
        c->started = true;
        Mel_Net_Listener_Result lr = mel_net_tcp_listen(c->net, .address = mel_net_address_v4_loopback(0), .backlog = 4, .reuse_addr = true);
        if (!mel_net_status_ok(lr.status))
        {
            c->finished = true;
            return;
        }
        c->listener = lr.value;
        net_cont_arm(c, &g_conts[0], mel_net_listener_accept(c->listener, .out_op = &c->op), cancel_on_accept);
        return;
    }
    if (c->turn == 5 && !c->cancel_requested_flag)
    {
        c->cancel_requested_flag = true;
        c->cancel_ok = mel_net_cancel(c->net, c->op);
        return;
    }
    if (c->accept_done && !c->finished)
    {
        mel_net_listener_destroy(c->listener);
        c->listener = NULL;
        c->finished = true;
    }
}

MEL_TEST(net, cancel_pending_accept_resolves_cancelled)
{
    Net_Ctx c = { 0 };
    c.step = cancel_step;

    net_run_ctx(&c);

    MEL_EXPECT(c.cancel_ok);
    MEL_EXPECT(c.accept_done);
    MEL_EXPECT(c.cancelled_seen);
}
