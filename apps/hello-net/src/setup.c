#include <boot/boot.h>

#include <allocator/allocator.h>
#include <collection/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <http/http.h>
#include <io/stream.h>
#include <log/log.h>
#include <net/address.h>
#include <net/net.h>
#include <net/resolve.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <string/str8.h>
#include <time/nano.h>
#include <vat/vat.h>

#include <string.h>

static const char* MEL_TAG = "hello-net";

#define SECONDS(n) ((i64)(n) * 1000 * 1000 * 1000)

typedef struct
{
    Mel_Task    task;
    Mel_Future* future;
    void (*fn)(Mel_Future* f);
} Cont;

typedef struct
{
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
    Mel_Net*         net;
    Mel_Http*        http;

    Mel_Net_Listener* listener;
    Mel_Net_Conn*     srv;
    Mel_Net_Conn*     cli;
    Mel_Net_Udp*      udp_a;
    Mel_Net_Udp*      udp_b;

    u8 udp_buf[64];
    u8 srv_buf[2048];
    u8 cli_buf[64];

    bool cli_ready;
    bool srv_ready;
    int  accepts;

    const char* url;
    bool        pass_addr;
    bool        pass_resolve;
    bool        pass_udp;
    bool        pass_tcp;
    bool        pass_http;
    bool        pass_url;
    bool        url_requested;
    bool        finished;

    Cont c_resolve;
    Cont c_udp_send;
    Cont c_udp_recv;
    Cont c_accept;
    Cont c_connect;
    Cont c_srv_io;
    Cont c_cli_io;
    Cont c_fetch;

    Mel_Vat_Source* watchdog;
    i64             deadline;
} G;

static G g;

static void cont_run(Mel_Task* self)
{
    Cont* k = mel_container_of(self, Cont, task);
    k->fn(k->future);
}

static void arm(Cont* k, Mel_Future* f, void (*fn)(Mel_Future*))
{
    k->future = f;
    k->fn = fn;
    mel_task_init(&k->task, cont_run);
    mel_future_then(f, &k->task, mel_net_executor(g.net));
}

static void stage_resolve(void);
static void stage_udp(void);
static void stage_tcp(void);
static void stage_http(void);
static void stage_url(void);
static void finish(void);

static void stage_addresses(void)
{
    Mel_Net_Address v4, v6;
    bool            ok = true;

    ok &= mel_net_status_ok(mel_net_address_parse(S8("192.168.1.10"), 8080, &v4));
    ok &= mel_net_status_ok(mel_net_address_parse(S8("2001:db8::ff00:42:8329"), 443, &v6));

    str8 f4 = mel_net_address_format(&v4, g.alloc);
    str8 f6 = mel_net_address_format(&v6, g.alloc);
    ok &= str8_equals(f4, S8("192.168.1.10"));
    ok &= str8_equals(f6, S8("2001:db8::ff00:42:8329"));
    ok &= mel_net_address_is_v4_mapped(&v6) == false;

    Mel_Net_Address lb = mel_net_address_v6_loopback(0);
    str8            flb = mel_net_address_format(&lb, g.alloc);
    ok &= str8_equals(flb, S8("::1"));

    mel_log_info(MEL_TAG, "[address] v4 %.*s:%u  v6 %.*s port %u  v6 loopback %.*s", (int)f4.len, f4.data, v4.port, (int)f6.len, f6.data, v6.port, (int)flb.len, flb.data);
    mel_dealloc(g.alloc, f4.data);
    mel_dealloc(g.alloc, f6.data);
    mel_dealloc(g.alloc, flb.data);

    g.pass_addr = ok;
    mel_log_info(MEL_TAG, "[address] %s", ok ? "PASS" : "FAIL");
}

static void on_resolved(Mel_Future* f)
{
    const Mel_Net_Resolve_Result* r = mel_net_future_resolve(f);
    if (mel_net_status_ok(r->status) && r->count > 0)
    {
        g.pass_resolve = true;
        for (usize i = 0; i < r->count; i++)
        {
            str8 t = mel_net_address_format(&r->items[i], g.alloc);
            mel_log_info(MEL_TAG, "[resolve] localhost -> %.*s (port %u)", (int)t.len, t.data, r->items[i].port);
            mel_dealloc(g.alloc, t.data);
            g.pass_resolve = g.pass_resolve && mel_net_address_is_loopback(&r->items[i]);
        }
    }
    else
    {
        mel_log_error(MEL_TAG, "[resolve] failed: status 0x%x os_error %d", r->status, r->os_error);
    }
    mel_net_future_release(f);
    mel_log_info(MEL_TAG, "[resolve] %s", g.pass_resolve ? "PASS" : "FAIL");
    stage_udp();
}

static void stage_resolve(void) { arm(&g.c_resolve, mel_net_resolve(g.net, S8("localhost"), .port = 80), on_resolved); }

static void on_udp_sent(Mel_Future* f) { mel_net_future_release(f); }

static void on_udp_received(Mel_Future* f)
{
    const Mel_Net_Udp_Result* r = mel_net_future_udp(f);
    if (mel_net_status_ok(r->status) && r->bytes == 13 && memcmp(g.udp_buf, "ping-over-udp", 13) == 0)
    {
        str8 from = mel_net_address_format(&r->from, g.alloc);
        mel_log_info(MEL_TAG, "[udp] %u byte datagram from %.*s:%u", (u32)r->bytes, (int)from.len, from.data, r->from.port);
        mel_dealloc(g.alloc, from.data);
        g.pass_udp = true;
    }
    else
    {
        mel_log_error(MEL_TAG, "[udp] failed: status 0x%x bytes %u", r->status, (u32)r->bytes);
    }
    mel_net_future_release(f);
    mel_net_udp_destroy(g.udp_a);
    g.udp_a = NULL;
    mel_net_udp_destroy(g.udp_b);
    g.udp_b = NULL;
    mel_log_info(MEL_TAG, "[udp] %s", g.pass_udp ? "PASS" : "FAIL");
    stage_tcp();
}

static void stage_udp(void)
{
    Mel_Net_Udp_Open_Result a = mel_net_udp_open(g.net, .address = mel_net_address_v4_loopback(0), .bind = true);
    Mel_Net_Udp_Open_Result b = mel_net_udp_open(g.net, .address = mel_net_address_v4_loopback(0), .bind = true);
    if (!mel_net_status_ok(a.status) || !mel_net_status_ok(b.status))
    {
        mel_log_error(MEL_TAG, "[udp] open failed");
        stage_tcp();
        return;
    }
    g.udp_a = a.value;
    g.udp_b = b.value;
    arm(&g.c_udp_recv, mel_net_udp_recv(g.udp_b, .buffer = g.udp_buf, .len = sizeof g.udp_buf), on_udp_received);
    arm(&g.c_udp_send, mel_net_udp_send(g.udp_a, .address = mel_net_udp_address(g.udp_b), .buffer = "ping-over-udp", .len = 13), on_udp_sent);
}

static void tcp_teardown_and_next(void)
{
    if (g.cli)
    {
        mel_net_conn_destroy(g.cli);
        g.cli = NULL;
    }
    if (g.srv)
    {
        mel_net_conn_destroy(g.srv);
        g.srv = NULL;
    }
    if (g.listener)
    {
        mel_net_listener_destroy(g.listener);
        g.listener = NULL;
    }
    g.cli_ready = false;
    g.srv_ready = false;
    mel_log_info(MEL_TAG, "[tcp] %s", g.pass_tcp ? "PASS" : "FAIL");
    stage_http();
}

static void on_tcp_cli_read(Mel_Future* f)
{
    const Mel_IO_Result* r = mel_stream_future_result(f);
    g.pass_tcp = !mel_io_status_failed(r->status) && r->bytes_transferred == 14 && memcmp(g.cli_buf, "MELODY SAYS HI", 14) == 0;
    mel_stream_future_release(f);
    if (g.pass_tcp)
        mel_log_info(MEL_TAG, "[tcp] echo round-trip through conn streams: \"MELODY SAYS HI\"");
    tcp_teardown_and_next();
}

static void on_tcp_srv_echoed(Mel_Future* f)
{
    mel_stream_future_release(f);
    arm(&g.c_cli_io, mel_stream_read(mel_net_conn_stream(g.cli), .buffer = g.cli_buf, .len = 14), on_tcp_cli_read);
}

static void on_tcp_srv_read(Mel_Future* f)
{
    const Mel_IO_Result* r = mel_stream_future_result(f);
    usize                n = r->bytes_transferred;
    bool                 ok = !mel_io_status_failed(r->status) && n == 14;
    mel_stream_future_release(f);
    if (!ok)
    {
        mel_log_error(MEL_TAG, "[tcp] server read failed");
        tcp_teardown_and_next();
        return;
    }
    for (usize i = 0; i < n; i++)
        if (g.srv_buf[i] >= 'a' && g.srv_buf[i] <= 'z')
            g.srv_buf[i] = (u8)(g.srv_buf[i] - 'a' + 'A');
    arm(&g.c_srv_io, mel_stream_write(mel_net_conn_stream(g.srv), .buffer = g.srv_buf, .len = n), on_tcp_srv_echoed);
}

static void on_tcp_cli_written(Mel_Future* f) { mel_stream_future_release(f); }

static void tcp_maybe_start_io(void)
{
    if (!g.cli_ready || !g.srv_ready)
        return;
    Mel_Net_Address peer = mel_net_conn_peer_address(g.cli);
    str8            t = mel_net_address_format(&peer, g.alloc);
    mel_log_info(MEL_TAG, "[tcp] connected to %.*s:%u (accept + connect on loopback)", (int)t.len, t.data, peer.port);
    mel_dealloc(g.alloc, t.data);
    arm(&g.c_srv_io, mel_stream_read(mel_net_conn_stream(g.srv), .buffer = g.srv_buf, .len = 14), on_tcp_srv_read);
    arm(&g.c_cli_io, mel_stream_write(mel_net_conn_stream(g.cli), .buffer = "melody says hi", .len = 14), on_tcp_cli_written);
}

static void on_tcp_accepted(Mel_Future* f)
{
    const Mel_Net_Conn_Result* r = mel_net_future_conn(f);
    bool                       ok = mel_net_status_ok(r->status);
    g.srv = mel_net_future_take_conn(f);
    mel_net_future_release(f);
    if (!ok || !g.srv)
    {
        mel_log_error(MEL_TAG, "[tcp] accept failed");
        tcp_teardown_and_next();
        return;
    }
    g.srv_ready = true;
    tcp_maybe_start_io();
}

static void on_tcp_connected(Mel_Future* f)
{
    const Mel_Net_Conn_Result* r = mel_net_future_conn(f);
    bool                       ok = mel_net_status_ok(r->status);
    g.cli = mel_net_future_take_conn(f);
    mel_net_future_release(f);
    if (!ok || !g.cli)
    {
        mel_log_error(MEL_TAG, "[tcp] connect failed");
        tcp_teardown_and_next();
        return;
    }
    g.cli_ready = true;
    tcp_maybe_start_io();
}

static void stage_tcp(void)
{
    Mel_Net_Listener_Result lr = mel_net_tcp_listen(g.net, .address = mel_net_address_v4_loopback(0), .backlog = 4, .reuse_addr = true);
    if (!mel_net_status_ok(lr.status))
    {
        mel_log_error(MEL_TAG, "[tcp] listen failed: status 0x%x", lr.status);
        stage_http();
        return;
    }
    g.listener = lr.value;
    arm(&g.c_accept, mel_net_listener_accept(g.listener), on_tcp_accepted);
    arm(&g.c_connect, mel_net_tcp_connect(g.net, .address = mel_net_listener_address(g.listener), .timeout_ns = SECONDS(5), .nodelay = true), on_tcp_connected);
}

static void http_srv_arm_accept(void);
static void http_srv_arm_read(void);

static void on_http_srv_written(Mel_Future* f)
{
    mel_stream_future_release(f);
    http_srv_arm_read();
}

static void on_http_srv_read(Mel_Future* f)
{
    const Mel_IO_Result* r = mel_stream_future_result(f);
    usize                n = r->bytes_transferred;
    bool                 eof = (r->status & MEL_IO_EOF) != 0 || mel_io_status_failed(r->status);
    mel_stream_future_release(f);

    if (eof)
    {
        if (g.srv)
        {
            mel_net_conn_destroy(g.srv);
            g.srv = NULL;
        }
        http_srv_arm_accept();
        return;
    }

    str8 req = { g.srv_buf, (size)n };
    bool is_hello = str8_contains(req, S8("GET /hello "));
    str8 resp = is_hello ? S8("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\n\r\n12\r\nhello from melody \r\ne\r\nover http/1.1\n\r\n0\r\n\r\n")
                         : S8("HTTP/1.1 302 Found\r\nLocation: /hello\r\nContent-Length: 0\r\n\r\n");
    mel_log_info(MEL_TAG, "[http] mini server answers %s", is_hello ? "200 (chunked)" : "302 -> /hello");
    arm(&g.c_srv_io, mel_stream_write(mel_net_conn_stream(g.srv), .buffer = resp.data, .len = (usize)resp.len), on_http_srv_written);
}

static void http_srv_arm_read(void)
{
    if (!g.srv)
        return;
    arm(&g.c_srv_io, mel_stream_read(mel_net_conn_stream(g.srv), .buffer = g.srv_buf, .len = sizeof g.srv_buf), on_http_srv_read);
}

static void on_http_accepted(Mel_Future* f)
{
    const Mel_Net_Conn_Result* r = mel_net_future_conn(f);
    if (mel_net_status_cancelled(r->status))
    {
        mel_net_future_release(f);
        return;
    }
    g.srv = mel_net_future_take_conn(f);
    mel_net_future_release(f);
    if (!g.srv)
        return;
    g.accepts++;
    http_srv_arm_read();
}

static void http_srv_arm_accept(void)
{
    if (!g.listener)
        return;
    arm(&g.c_accept, mel_net_listener_accept(g.listener), on_http_accepted);
}

static void http_teardown_and_next(void)
{
    if (g.srv)
    {
        mel_net_conn_destroy(g.srv);
        g.srv = NULL;
    }
    if (g.listener)
    {
        mel_net_listener_destroy(g.listener);
        g.listener = NULL;
    }
    mel_log_info(MEL_TAG, "[http] %s", g.pass_http ? "PASS" : "FAIL");
    stage_url();
}

static void on_http_fetched(Mel_Future* f)
{
    const Mel_Http_Result* r = mel_http_future_result(f);
    str8                   ctype = STR8_EMPTY;
    mel_http_result_header(r, S8("Content-Type"), &ctype);
    bool ok = mel_http_status_ok(r->status) && r->status_code == 200 && str8_equals(r->body, S8("hello from melody over http/1.1\n")) && g.accepts == 1;
    if (ok)
        mel_log_info(MEL_TAG, "[http] fetch followed the redirect, decoded chunked body (%u bytes, %.*s), reused one keep-alive conn for both requests", (u32)r->body.len, (int)ctype.len, ctype.data);
    else
        mel_log_error(MEL_TAG, "[http] fetch failed: status 0x%x code %d accepts %d", r->status, r->status_code, g.accepts);
    mel_http_future_release(f);
    g.pass_http = ok;
    http_teardown_and_next();
}

static void stage_http(void)
{
    Mel_Net_Listener_Result lr = mel_net_tcp_listen(g.net, .address = mel_net_address_v4_loopback(0), .backlog = 4, .reuse_addr = true);
    if (!mel_net_status_ok(lr.status))
    {
        mel_log_error(MEL_TAG, "[http] listen failed");
        stage_url();
        return;
    }
    g.listener = lr.value;
    g.accepts = 0;
    http_srv_arm_accept();

    Mel_Net_Address bound = mel_net_listener_address(g.listener);
    str8            url = str8_fmt_alloc(g.alloc, "http://127.0.0.1:%u/", (u32)bound.port);
    mel_log_info(MEL_TAG, "[http] mini server on %.*s, fetching with max_redirects=2", (int)url.len, url.data);
    arm(&g.c_fetch, mel_http_fetch(g.http, ((Mel_Http_Request){ .method = S8("GET"), .url = url }), .total_timeout_ns = SECONDS(5), .max_redirects = 2), on_http_fetched);
    mel_dealloc(g.alloc, url.data);
}

static void on_url_fetched(Mel_Future* f)
{
    const Mel_Http_Result* r = mel_http_future_result(f);
    if (mel_http_status_ok(r->status))
    {
        str8 ctype = STR8_EMPTY;
        mel_http_result_header(r, S8("Content-Type"), &ctype);
        usize preview = r->body.len < 80 ? (usize)r->body.len : 80;
        mel_log_info(MEL_TAG, "[url] %s -> %d %.*s, %u header(s), %u body byte(s), type %.*s", g.url, r->status_code, (int)r->reason.len, r->reason.data, (u32)r->header_count, (u32)r->body.len, (int)ctype.len, ctype.data);
        mel_log_info(MEL_TAG, "[url] body starts: %.*s", (int)preview, r->body.data);
        g.pass_url = true;
    }
    else
    {
        mel_log_error(MEL_TAG, "[url] fetch failed: status 0x%x os_error %d%s", r->status, r->os_error, (r->status & MEL_HTTP_TLS_FAILED) ? " (https needs the tls layer; use an http:// url)" : "");
    }
    mel_http_future_release(f);
    mel_log_info(MEL_TAG, "[url] %s", g.pass_url ? "PASS" : "FAIL");
    finish();
}

static void stage_url(void)
{
    if (!g.url)
    {
        finish();
        return;
    }
    g.url_requested = true;
    mel_log_info(MEL_TAG, "[url] fetching %s", g.url);
    arm(&g.c_fetch, mel_http_fetch(g.http, ((Mel_Http_Request){ .url = str8_from_cstr(g.url) }), .connect_timeout_ns = SECONDS(5), .total_timeout_ns = SECONDS(10), .max_redirects = 4, .max_body_bytes = 1 << 20), on_url_fetched);
}

static void finish(void)
{
    g.finished = true;
    bool ok = g.pass_addr && g.pass_resolve && g.pass_udp && g.pass_tcp && g.pass_http && (!g.url_requested || g.pass_url);
    mel_log_info(MEL_TAG,
                 "summary: address %s, resolve %s, udp %s, tcp %s, http %s%s%s",
                 g.pass_addr ? "PASS" : "FAIL",
                 g.pass_resolve ? "PASS" : "FAIL",
                 g.pass_udp ? "PASS" : "FAIL",
                 g.pass_tcp ? "PASS" : "FAIL",
                 g.pass_http ? "PASS" : "FAIL",
                 g.url_requested ? ", url " : "",
                 g.url_requested ? (g.pass_url ? "PASS" : "FAIL") : "");
    mel_app_set_exit_code(ok ? 0 : 1);
    mel_vat_quit(g.vat);
}

static i64 watchdog_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return g.deadline;
}

static bool watchdog_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)s;
    (void)budget;
    if (g.finished || (i64)mel_nanos_since_unspecified_epoch() < g.deadline)
        return false;
    mel_log_error(MEL_TAG, "watchdog: demo did not finish in time");
    mel_app_set_exit_code(1);
    mel_vat_quit(g.vat);
    return false;
}

static const Mel_Vat_Source_Vtbl WATCHDOG_VT = {
    .wakeables = NULL,
    .deadline = watchdog_deadline,
    .drain = watchdog_drain,
    .cancel = NULL,
};

static void app_teardown(void* user)
{
    (void)user;
    if (g.http)
    {
        mel_http_destroy(g.http);
        g.http = NULL;
    }
    if (g.cli)
    {
        mel_net_conn_destroy(g.cli);
        g.cli = NULL;
    }
    if (g.srv)
    {
        mel_net_conn_destroy(g.srv);
        g.srv = NULL;
    }
    if (g.listener)
    {
        mel_net_listener_destroy(g.listener);
        g.listener = NULL;
    }
    if (g.udp_a)
    {
        mel_net_udp_destroy(g.udp_a);
        g.udp_a = NULL;
    }
    if (g.udp_b)
    {
        mel_net_udp_destroy(g.udp_b);
        g.udp_b = NULL;
    }
    if (g.net)
    {
        mel_net_destroy(g.net);
        g.net = NULL;
    }
    if (g.watchdog)
    {
        mel_vat_source_close(g.watchdog);
        g.watchdog = NULL;
    }
}

void mel_app_setup(Mel_Vat* root)
{
    g.vat = root;
    g.alloc = mel_vat_alloc(root);
    g.url = mel_app_argc() >= 2 ? mel_app_argv()[1] : NULL;

    g.net = mel_net_create(.vat = root, .resolver_workers = 1);
    if (!g.net || !mel_net_available(g.net))
    {
        mel_log_error(MEL_TAG, "net is unavailable on this platform");
        mel_app_set_exit_code(1);
        return;
    }
    g.http = mel_http_create(.net = g.net, .max_conns_per_host = 2);
    if (!g.http)
    {
        mel_log_error(MEL_TAG, "http create failed");
        mel_app_set_exit_code(1);
        return;
    }

    mel_vat_retain(root);
    mel_app_on_exit(app_teardown, NULL);

    g.deadline = (i64)mel_nanos_since_unspecified_epoch() + SECONDS(30);
    g.watchdog = mel_vat_source_open(root, &WATCHDOG_VT, &g);

    mel_log_info(MEL_TAG, "melody net+http feature tour%s", g.url ? " (with a real fetch at the end)" : " (pass an http:// url for a real fetch too)");
    stage_addresses();
    stage_resolve();
}
