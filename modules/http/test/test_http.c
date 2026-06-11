#include <http/http.h>
#include <http/url.h>

#include "../src/wire.h"

#include <allocator/heap.h>
#include <collection/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <io/stream.h>
#include <net/address.h>
#include <net/net.h>
#include <net/tcp.h>
#include <string/str8.h>
#include <test/test.h>
#include <vat/vat.h>

#include <string.h>

MEL_TEST(http, url_parse_full)
{
    Mel_Http_Url u;
    MEL_REQUIRE(mel_http_status_ok(mel_http_url_parse(S8("http://user@example.com:8080/a/b?x=1&y=2#frag"), &u)));
    MEL_EXPECT_EQ_STR8(u.scheme, S8("http"));
    MEL_EXPECT_EQ_STR8(u.userinfo, S8("user"));
    MEL_EXPECT_EQ_STR8(u.host, S8("example.com"));
    MEL_EXPECT(u.port_explicit);
    MEL_EXPECT_EQ(u.port, 8080);
    MEL_EXPECT_EQ_STR8(u.path, S8("/a/b"));
    MEL_EXPECT_EQ_STR8(u.query, S8("x=1&y=2"));
    MEL_EXPECT_EQ_STR8(u.fragment, S8("frag"));
    MEL_EXPECT_EQ(mel_http_url_effective_port(&u), 8080);
}

MEL_TEST(http, url_parse_defaults_and_v6)
{
    Mel_Http_Url u;
    MEL_REQUIRE(mel_http_status_ok(mel_http_url_parse(S8("https://example.com"), &u)));
    MEL_EXPECT(!u.port_explicit);
    MEL_EXPECT_EQ(mel_http_url_effective_port(&u), 443);
    MEL_EXPECT_EQ((i64)u.path.len, (i64)0);

    MEL_REQUIRE(mel_http_status_ok(mel_http_url_parse(S8("http://[::1]:9000/x"), &u)));
    MEL_EXPECT_EQ_STR8(u.host, S8("[::1]"));
    MEL_EXPECT_EQ(u.port, 9000);

    MEL_EXPECT(mel_http_status_failed(mel_http_url_parse(S8("example.com/x"), &u)));
    MEL_EXPECT(mel_http_status_failed(mel_http_url_parse(S8("http://"), &u)));
    MEL_EXPECT(mel_http_status_failed(mel_http_url_parse(S8("http://host:99999/"), &u)));
}

MEL_TEST(http, percent_roundtrip)
{
    const Mel_Alloc* a = mel_alloc_heap();
    str8             enc = mel_http_percent_encode(S8("a b/c~d"), a);
    MEL_EXPECT_EQ_STR8(enc, S8("a%20b%2Fc~d"));

    str8 dec;
    MEL_REQUIRE(mel_http_status_ok(mel_http_percent_decode(enc, a, &dec)));
    MEL_EXPECT_EQ_STR8(dec, S8("a b/c~d"));
    mel_dealloc(a, enc.data);
    mel_dealloc(a, dec.data);

    MEL_EXPECT(mel_http_status_failed(mel_http_percent_decode(S8("bad%2"), a, &dec)));
    MEL_EXPECT(mel_http_status_failed(mel_http_percent_decode(S8("bad%zz"), a, &dec)));
}

MEL_TEST(http, wire_request_head_shape)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Http_Header  hs[] = { { S8("Accept"), S8("*/*") } };
    str8             head = mel_http__wire_request_head(a, S8("POST"), S8("/submit?x=1"), S8("example.com"), 8080, false, hs, 1, 5);
    MEL_EXPECT_EQ_STR8(head, S8("POST /submit?x=1 HTTP/1.1\r\nHost: example.com:8080\r\nAccept: */*\r\nContent-Length: 5\r\n\r\n"));
    mel_dealloc(a, head.data);

    str8 head2 = mel_http__wire_request_head(a, S8("GET"), S8("/"), S8("example.com"), 80, true, NULL, 0, 0);
    MEL_EXPECT_EQ_STR8(head2, S8("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"));
    mel_dealloc(a, head2.data);
}

typedef struct
{
    Mel_Http__Buf out;
} Body_Sink;

static void body_collect(void* user, str8 span)
{
    Body_Sink* s = (Body_Sink*)user;
    for (usize i = 0; i < span.len; i++)
        mel_array_push(&s->out, span.data[i]);
}

MEL_TEST(http, wire_parser_content_length)
{
    const Mel_Alloc*      a = mel_alloc_heap();
    Mel_Http__Resp_Parser p;
    mel_http__parser_init(&p, a, 64 * 1024, false);
    Body_Sink sink = { 0 };
    mel_array_init(&sink.out, a);

    str8  msg = S8("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nhello world");
    usize consumed = 0;
    MEL_REQUIRE(mel_http_status_ok(mel_http__parser_feed(&p, msg.data, msg.len, &consumed, body_collect, &sink)));
    MEL_EXPECT_EQ((i64)consumed, (i64)msg.len);
    MEL_EXPECT(p.done);
    MEL_EXPECT_EQ(p.status_code, 200);
    MEL_EXPECT_EQ_STR8(p.reason, S8("OK"));
    MEL_EXPECT_EQ((i64)p.headers.count, (i64)2);
    MEL_EXPECT_EQ_STR8(((str8){ sink.out.items, (size)sink.out.count }), S8("hello world"));
    MEL_EXPECT(mel_http__parser_keep_alive(&p));

    mel_array_free(&sink.out);
    mel_http__parser_free(&p);
}

MEL_TEST(http, wire_parser_chunked_with_trailer_byte_by_byte)
{
    const Mel_Alloc*      a = mel_alloc_heap();
    Mel_Http__Resp_Parser p;
    mel_http__parser_init(&p, a, 64 * 1024, false);
    Body_Sink sink = { 0 };
    mel_array_init(&sink.out, a);

    str8 msg = S8("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;ext=1\r\nhello\r\n6\r\n world\r\n0\r\nX-Trailer: v\r\n\r\n");
    for (usize i = 0; i < msg.len; i++)
    {
        usize consumed = 0;
        MEL_REQUIRE(mel_http_status_ok(mel_http__parser_feed(&p, msg.data + i, 1, &consumed, body_collect, &sink)));
        MEL_REQUIRE_EQ((i64)consumed, (i64)1);
    }
    MEL_EXPECT(p.done);
    MEL_EXPECT_EQ_STR8(((str8){ sink.out.items, (size)sink.out.count }), S8("hello world"));

    mel_array_free(&sink.out);
    mel_http__parser_free(&p);
}

MEL_TEST(http, wire_parser_no_body_and_interim)
{
    const Mel_Alloc*      a = mel_alloc_heap();
    Mel_Http__Resp_Parser p;
    mel_http__parser_init(&p, a, 64 * 1024, false);

    str8  msg = S8("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
    usize consumed = 0;
    MEL_REQUIRE(mel_http_status_ok(mel_http__parser_feed(&p, msg.data, msg.len, &consumed, NULL, NULL)));
    MEL_EXPECT(p.done);
    MEL_EXPECT_EQ(p.status_code, 204);
    MEL_EXPECT(!mel_http__parser_keep_alive(&p));
    mel_http__parser_free(&p);

    mel_http__parser_init(&p, a, 64 * 1024, true);
    str8 head_resp = S8("HTTP/1.1 200 OK\r\nContent-Length: 999\r\n\r\n");
    MEL_REQUIRE(mel_http_status_ok(mel_http__parser_feed(&p, head_resp.data, head_resp.len, &consumed, NULL, NULL)));
    MEL_EXPECT(p.done);
    mel_http__parser_free(&p);
}

MEL_TEST(http, wire_parser_eof_body_and_malformed)
{
    const Mel_Alloc*      a = mel_alloc_heap();
    Mel_Http__Resp_Parser p;
    mel_http__parser_init(&p, a, 64 * 1024, false);
    Body_Sink sink = { 0 };
    mel_array_init(&sink.out, a);

    str8  msg = S8("HTTP/1.0 200 OK\r\n\r\nuntil-close");
    usize consumed = 0;
    MEL_REQUIRE(mel_http_status_ok(mel_http__parser_feed(&p, msg.data, msg.len, &consumed, body_collect, &sink)));
    MEL_EXPECT(!p.done);
    MEL_REQUIRE(mel_http_status_ok(mel_http__parser_finish_eof(&p)));
    MEL_EXPECT(p.done);
    MEL_EXPECT(!mel_http__parser_keep_alive(&p));
    MEL_EXPECT_EQ_STR8(((str8){ sink.out.items, (size)sink.out.count }), S8("until-close"));
    mel_array_free(&sink.out);
    mel_http__parser_free(&p);

    mel_http__parser_init(&p, a, 64 * 1024, false);
    str8 bad = S8("ICMP/9 hi\r\n\r\n");
    MEL_EXPECT(mel_http_status_failed(mel_http__parser_feed(&p, bad.data, bad.len, &consumed, NULL, NULL)));
    mel_http__parser_free(&p);

    mel_http__parser_init(&p, a, 32, false);
    str8 huge = S8("HTTP/1.1 200 OK\r\nX-Big: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r\n");
    MEL_EXPECT(mel_http_status_failed(mel_http__parser_feed(&p, huge.data, huge.len, &consumed, NULL, NULL)));
    mel_http__parser_free(&p);
}

typedef struct Http_Ctx Http_Ctx;

typedef struct
{
    Mel_Task    task;
    Mel_Future* future;
    Http_Ctx*   ctx;
    void (*fn)(Http_Ctx* c, Mel_Future* f);
} Http_Cont;

struct Http_Ctx
{
    Mel_Vat*      vat;
    Mel_Net*      net;
    Mel_Http*     http;
    Mel_Executor* exec;
    int           turn;
    bool          finished;
    void (*step)(Http_Ctx* c);

    Mel_Net_Listener* listener;
    Mel_Net_Conn*     srv_conn;
    u8                srv_buf[4096];
    str8              responses[4];
    usize             response_count;
    usize             served;
    bool              close_after_response;
    int               accepts;
    bool              srv_reading;

    bool            fetch_started;
    bool            fetch_done;
    bool            second_started;
    bool            second_done;
    Mel_Http_Result res;
    str8            res_body_copy;
    str8            res_ctype_copy;
    bool            fetch_cancelled_seen;

    Http_Cont conts[8];
    usize     cont_next;
};

static void http_cont_run(Mel_Task* self)
{
    Http_Cont* k = mel_container_of(self, Http_Cont, task);
    k->fn(k->ctx, k->future);
}

static Http_Cont* ctx_cont(Http_Ctx* c)
{
    Http_Cont* k = &c->conts[c->cont_next];
    c->cont_next = (c->cont_next + 1) % 8;
    return k;
}

static void cont_arm(Http_Ctx* c, Mel_Future* f, void (*fn)(Http_Ctx*, Mel_Future*))
{
    Http_Cont* k = ctx_cont(c);
    k->ctx = c;
    k->future = f;
    k->fn = fn;
    mel_task_init(&k->task, http_cont_run);
    mel_future_then(f, &k->task, c->exec);
}

static void srv_arm_accept(Http_Ctx* c);
static void srv_arm_read(Http_Ctx* c);

static void srv_on_write(Http_Ctx* c, Mel_Future* f)
{
    mel_stream_future_release(f);
    if (c->close_after_response)
    {
        mel_net_conn_destroy(c->srv_conn);
        c->srv_conn = NULL;
        srv_arm_accept(c);
        return;
    }
    srv_arm_read(c);
}

static void srv_on_read(Http_Ctx* c, Mel_Future* f)
{
    const Mel_IO_Result* r = mel_stream_future_result(f);
    bool                 eof = (r->status & MEL_IO_EOF) != 0 || mel_io_status_failed(r->status);
    mel_stream_future_release(f);
    c->srv_reading = false;

    if (eof)
    {
        mel_net_conn_destroy(c->srv_conn);
        c->srv_conn = NULL;
        srv_arm_accept(c);
        return;
    }
    if (c->served < c->response_count)
    {
        str8 resp = c->responses[c->served++];
        cont_arm(c, mel_stream_write(mel_net_conn_stream(c->srv_conn), .buffer = resp.data, .len = (usize)resp.len), srv_on_write);
        return;
    }
    srv_arm_read(c);
}

static void srv_arm_read(Http_Ctx* c)
{
    c->srv_reading = true;
    cont_arm(c, mel_stream_read(mel_net_conn_stream(c->srv_conn), .buffer = c->srv_buf, .len = sizeof c->srv_buf), srv_on_read);
}

static void srv_on_accept(Http_Ctx* c, Mel_Future* f)
{
    const Mel_Net_Conn_Result* r = mel_net_future_conn(f);
    if (mel_net_status_cancelled(r->status))
    {
        mel_net_future_release(f);
        return;
    }
    c->srv_conn = mel_net_future_take_conn(f);
    mel_net_future_release(f);
    if (!c->srv_conn)
        return;
    c->accepts++;
    srv_arm_read(c);
}

static void srv_arm_accept(Http_Ctx* c)
{
    if (!c->listener)
        return;
    cont_arm(c, mel_net_listener_accept(c->listener), srv_on_accept);
}

static str8 ctx_url(Http_Ctx* c, const char* path)
{
    Mel_Net_Address bound = mel_net_listener_address(c->listener);
    return str8_fmt_alloc(mel_alloc_heap(), "http://127.0.0.1:%u%s", (u32)bound.port, path);
}

static void fetch_capture(Http_Ctx* c, Mel_Future* f)
{
    c->res = *mel_http_future_result(f);
    c->fetch_cancelled_seen = mel_future_status_cancelled(mel_future_status(f));
    c->res_body_copy = str8_dup_alloc(c->res.body, mel_alloc_heap());
    str8 ctype;
    if (mel_http_result_header(&c->res, S8("Content-Type"), &ctype))
        c->res_ctype_copy = str8_dup_alloc(ctype, mel_alloc_heap());
    mel_http_future_release(f);
    c->fetch_done = true;
}

static bool ctx_idle(void* user)
{
    Http_Ctx* c = (Http_Ctx*)user;
    c->turn++;
    c->step(c);
    if (c->finished)
    {
        if (c->http)
        {
            mel_http_destroy(c->http);
            c->http = NULL;
        }
        if (c->srv_conn)
        {
            mel_net_conn_destroy(c->srv_conn);
            c->srv_conn = NULL;
        }
        if (c->listener)
        {
            mel_net_listener_destroy(c->listener);
            c->listener = NULL;
        }
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

static i64 ctx_idle_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return 0;
}

static bool ctx_idle_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    ctx_idle(mel_vat_source_state(s));
    return false;
}

static const Mel_Vat_Source_Vtbl CTX_IDLE_VT = {
    .wakeables = NULL,
    .deadline = ctx_idle_deadline,
    .drain = ctx_idle_drain,
    .cancel = NULL,
};

static void ctx_run(Http_Ctx* c)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Vat_Waiter*  waiter = mel_vat_waiter_io(a);
    Mel_Vat_Driver*  driver = mel_vat_driver_fair(a, 64);
    Mel_Vat*         vat = mel_vat_open(a, (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    c->vat = vat;
    c->net = mel_net_create(.vat = vat, .resolver_workers = 1);
    c->http = mel_http_create(.net = c->net, .max_conns_per_host = 2);
    c->exec = mel_net_executor(c->net);

    Mel_Net_Listener_Result lr = mel_net_tcp_listen(c->net, .address = mel_net_address_v4_loopback(0), .backlog = 8, .reuse_addr = true);
    c->listener = lr.value;
    if (c->listener)
        srv_arm_accept(c);

    Mel_Vat_Source* idle = mel_vat_source_open(vat, &CTX_IDLE_VT, c);
    mel_vat_run(vat);
    mel_vat_source_close(idle);
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
}

static void ctx_free_copies(Http_Ctx* c)
{
    if (c->res_body_copy.data)
        mel_dealloc(mel_alloc_heap(), c->res_body_copy.data);
    if (c->res_ctype_copy.data)
        mel_dealloc(mel_alloc_heap(), c->res_ctype_copy.data);
}

#define HTTP_T5S ((i64)5 * 1000 * 1000 * 1000)

static void get_step(Http_Ctx* c)
{
    if (c->turn == 2 && !c->fetch_started && c->listener)
    {
        c->fetch_started = true;
        str8 url = ctx_url(c, "/hello");
        cont_arm(c, mel_http_fetch(c->http, ((Mel_Http_Request){ .method = S8("GET"), .url = url }), .total_timeout_ns = HTTP_T5S), fetch_capture);
        mel_dealloc(mel_alloc_heap(), url.data);
        return;
    }
    if (c->fetch_done && !c->finished)
        c->finished = true;
}

MEL_TEST(http, fetch_get_collects_body)
{
    Http_Ctx c = { 0 };
    c.step = get_step;
    c.responses[0] = S8("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nhello world");
    c.response_count = 1;

    ctx_run(&c);

    MEL_EXPECT(c.fetch_done);
    MEL_EXPECT(mel_http_status_ok(c.res.status));
    MEL_EXPECT_EQ(c.res.status_code, 200);
    MEL_EXPECT_EQ_STR8(c.res_body_copy, S8("hello world"));
    MEL_EXPECT_EQ_STR8(c.res_ctype_copy, S8("text/plain"));
    MEL_EXPECT_EQ(c.accepts, 1);
    ctx_free_copies(&c);
}

static void second_capture(Http_Ctx* c, Mel_Future* f)
{
    c->res = *mel_http_future_result(f);
    if (c->res_body_copy.data)
        mel_dealloc(mel_alloc_heap(), c->res_body_copy.data);
    c->res_body_copy = str8_dup_alloc(c->res.body, mel_alloc_heap());
    mel_http_future_release(f);
    c->second_done = true;
}

static void keepalive_step(Http_Ctx* c)
{
    if (c->turn == 2 && !c->fetch_started && c->listener)
    {
        c->fetch_started = true;
        str8 url = ctx_url(c, "/one");
        cont_arm(c, mel_http_fetch(c->http, ((Mel_Http_Request){ .url = url }), .total_timeout_ns = HTTP_T5S), fetch_capture);
        mel_dealloc(mel_alloc_heap(), url.data);
        return;
    }
    if (c->fetch_done && !c->second_started)
    {
        c->second_started = true;
        str8 url = ctx_url(c, "/two");
        cont_arm(c, mel_http_fetch(c->http, ((Mel_Http_Request){ .url = url }), .total_timeout_ns = HTTP_T5S), second_capture);
        mel_dealloc(mel_alloc_heap(), url.data);
        return;
    }
    if (c->second_done && !c->finished)
        c->finished = true;
}

MEL_TEST(http, fetch_keep_alive_reuses_connection)
{
    Http_Ctx c = { 0 };
    c.step = keepalive_step;
    c.responses[0] = S8("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none");
    c.responses[1] = S8("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
    c.response_count = 2;

    ctx_run(&c);

    MEL_EXPECT(c.fetch_done);
    MEL_EXPECT(c.second_done);
    MEL_EXPECT(mel_http_status_ok(c.res.status));
    MEL_EXPECT_EQ_STR8(c.res_body_copy, S8("two"));
    MEL_EXPECT_EQ(c.accepts, 1);
    ctx_free_copies(&c);
}

MEL_TEST(http, fetch_chunked_body)
{
    Http_Ctx c = { 0 };
    c.step = get_step;
    c.responses[0] = S8("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
    c.response_count = 1;

    ctx_run(&c);

    MEL_EXPECT(c.fetch_done);
    MEL_EXPECT(mel_http_status_ok(c.res.status));
    MEL_EXPECT_EQ_STR8(c.res_body_copy, S8("hello world"));
    ctx_free_copies(&c);
}

static void redirect_step(Http_Ctx* c)
{
    if (c->turn == 2 && !c->fetch_started && c->listener)
    {
        c->fetch_started = true;
        str8 url = ctx_url(c, "/old");
        cont_arm(c, mel_http_fetch(c->http, ((Mel_Http_Request){ .url = url }), .total_timeout_ns = HTTP_T5S, .max_redirects = 2), fetch_capture);
        mel_dealloc(mel_alloc_heap(), url.data);
        return;
    }
    if (c->fetch_done && !c->finished)
        c->finished = true;
}

MEL_TEST(http, fetch_follows_redirect)
{
    Http_Ctx c = { 0 };
    c.step = redirect_step;
    c.responses[0] = S8("HTTP/1.1 302 Found\r\nLocation: /new\r\nContent-Length: 0\r\n\r\n");
    c.responses[1] = S8("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nmoved");
    c.response_count = 2;

    ctx_run(&c);

    MEL_EXPECT(c.fetch_done);
    MEL_EXPECT(mel_http_status_ok(c.res.status));
    MEL_EXPECT_EQ(c.res.status_code, 200);
    MEL_EXPECT_EQ_STR8(c.res_body_copy, S8("moved"));
    ctx_free_copies(&c);
}

MEL_TEST(http, fetch_redirects_not_followed_by_default)
{
    Http_Ctx c = { 0 };
    c.step = get_step;
    c.responses[0] = S8("HTTP/1.1 302 Found\r\nLocation: /new\r\nContent-Length: 0\r\n\r\n");
    c.response_count = 1;

    ctx_run(&c);

    MEL_EXPECT(c.fetch_done);
    MEL_EXPECT(mel_http_status_ok(c.res.status));
    MEL_EXPECT_EQ(c.res.status_code, 302);
    ctx_free_copies(&c);
}

MEL_TEST(http, fetch_eof_delimited_body)
{
    Http_Ctx c = { 0 };
    c.step = get_step;
    c.responses[0] = S8("HTTP/1.1 200 OK\r\n\r\nuntil-close");
    c.response_count = 1;
    c.close_after_response = true;

    ctx_run(&c);

    MEL_EXPECT(c.fetch_done);
    MEL_EXPECT(mel_http_status_ok(c.res.status));
    MEL_EXPECT_EQ_STR8(c.res_body_copy, S8("until-close"));
    ctx_free_copies(&c);
}

MEL_TEST(http, fetch_4xx_is_successful_transfer)
{
    Http_Ctx c = { 0 };
    c.step = get_step;
    c.responses[0] = S8("HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found");
    c.response_count = 1;

    ctx_run(&c);

    MEL_EXPECT(c.fetch_done);
    MEL_EXPECT(mel_http_status_ok(c.res.status));
    MEL_EXPECT_EQ(c.res.status_code, 404);
    MEL_EXPECT_EQ_STR8(c.res_body_copy, S8("not found"));
    ctx_free_copies(&c);
}
