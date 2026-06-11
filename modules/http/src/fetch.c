#include "wire.h"

#include <http/http.h>
#include <http/url.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <collection/list.h>
#include <collection/slotmap.h>
#include <executor/executor.h>
#include <future/future.h>
#include <io/stream.h>
#include <log/log.h>
#include <net/net.h>
#include <net/address.h>
#include <net/resolve.h>
#include <net/tcp.h>
#include <string/str8.h>
#include <time/nano.h>
#include <vat/vat.h>

#include <assert.h>
#include <string.h>

#define MEL_HTTP__READ_CHUNK 16384

typedef struct Mel_Http_Op_Record Mel_Http_Op_Record;
typedef struct Http_Bucket        Http_Bucket;

typedef struct
{
    Mel_Net_Conn* conn;
    Http_Bucket*  bucket;
    i64           idle_since;
} Http_Conn;

struct Http_Bucket
{
    str8 key;
    Mel_Array(Http_Conn*) idle;
    u32 active;
    Mel_Array(Mel_Http_Op_Record*) waiting;
};

struct Mel_Http
{
    Mel_Net*         net;
    const Mel_Alloc* alloc;
    Mel_SlotMap      ops;
    Mel_Array(Http_Bucket*) buckets;
    u32             max_conns_per_host;
    i64             idle_timeout;
    usize           max_head;
    Mel_Vat_Source* sweep;
    bool            zero_timeout_logged;
    bool            destroying;
};

struct Mel_Http_Op_Record
{
    Mel_Http*          http;
    const Mel_Alloc*   alloc;
    Mel_SlotMap_Handle self;
    Mel_Executor*      deliver;

    Mel_Future      future;
    Mel_Http_Result result;

    bool settled;
    bool detached;
    bool released;
    u32  refs;

    str8             method;
    str8             url_text;
    Mel_Http_Header* req_headers;
    usize            req_header_count;
    str8             body;
    Mel_Stream*      sink;

    i64   connect_timeout;
    i64   response_timeout;
    i64   total_timeout;
    u32   redirects_left;
    bool  allow_insecure_redirect;
    usize max_body;

    Mel_Http_Url url;
    Http_Conn*   conn;
    Http_Bucket* bucket;
    bool         reused;
    bool         retried;
    bool         waiting_in_bucket;
    bool         response_bytes_seen;
    bool         parser_live;

    Mel_Http__Resp_Parser parser;
    Mel_Http__Buf         body_buf;
    Mel_Http__Buf         sink_staging;
    Mel_Http_Status       body_error;
    str8                  head_bytes;
    u8*                   read_buf;

    Mel_Net_Op net_op;
    bool       net_op_live;
    bool       resolving;

    Mel_Vat_Source* timer;
    i64             deadline_total;
    i64             deadline_response;

    Mel_Task    io_task;
    Mel_Future* io_future;
    void (*on_io)(Mel_Http_Op_Record* op, Mel_IO_Result r);

    Mel_Task    net_task;
    Mel_Future* net_future;
    void (*on_net)(Mel_Http_Op_Record* op, Mel_Future* f);
};

static void fetch_acquire(Mel_Http_Op_Record* op);
static void fetch_send(Mel_Http_Op_Record* op);
static void fetch_read_step(Mel_Http_Op_Record* op);
static void bucket_pump(Mel_Http* http, Http_Bucket* bucket);

static Mel_Executor* loop_exec(Mel_Http_Op_Record* op) { return mel_net_executor(op->http->net); }

static i64 now_ns(void) { return (i64)mel_nanos_since_unspecified_epoch(); }

static Mel_Future_Status http_future_status_from(Mel_Http_Status status)
{
    Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
    if (status & MEL_HTTP_TIMED_OUT)
        fs |= MEL_FUTURE_TIMED_OUT;
    return fs;
}

static void op_free(Mel_Http_Op_Record* op)
{
    const Mel_Alloc* a = op->alloc;
    if (op->parser_live)
        mel_http__parser_free(&op->parser);
    mel_array_free(&op->body_buf);
    mel_array_free(&op->sink_staging);
    if (op->head_bytes.data)
        mel_dealloc(a, op->head_bytes.data);
    if (op->method.data)
        mel_dealloc(a, op->method.data);
    if (op->url_text.data)
        mel_dealloc(a, op->url_text.data);
    if (op->body.data)
        mel_dealloc(a, op->body.data);
    if (op->req_headers)
    {
        for (usize i = 0; i < op->req_header_count; i++)
        {
            if (op->req_headers[i].name.data)
                mel_dealloc(a, op->req_headers[i].name.data);
            if (op->req_headers[i].value.data)
                mel_dealloc(a, op->req_headers[i].value.data);
        }
        mel_dealloc(a, op->req_headers);
    }
    if (op->read_buf)
        mel_dealloc(a, op->read_buf);
    mel_dealloc(a, op);
}

static void op_unref(Mel_Http_Op_Record* op)
{
    assert(op->refs > 0);
    op->refs--;
    if (op->released && op->refs == 0)
        op_free(op);
}

static Http_Conn* conn_wrap(Mel_Http* http, Mel_Net_Conn* nc, Http_Bucket* bucket)
{
    Http_Conn* c = mel_alloc_type(http->alloc, Http_Conn);
    if (!c)
    {
        mel_net_conn_destroy(nc);
        return NULL;
    }
    memset(c, 0, sizeof *c);
    c->conn = nc;
    c->bucket = bucket;
    return c;
}

static void conn_destroy(Mel_Http* http, Http_Conn* c)
{
    Http_Bucket* bucket = c->bucket;
    mel_net_conn_destroy(c->conn);
    mel_dealloc(http->alloc, c);
    assert(bucket->active > 0);
    bucket->active--;
    if (!http->destroying)
        bucket_pump(http, bucket);
}

static void conn_pool_return(Mel_Http* http, Http_Conn* c)
{
    c->idle_since = now_ns();
    mel_array_push(&c->bucket->idle, c);
    if (http->sweep)
        mel_vat_source_demand_changed(http->sweep);
    if (!http->destroying)
        bucket_pump(http, c->bucket);
}

static void op_stop_io(Mel_Http_Op_Record* op)
{
    op->on_io = NULL;
    op->on_net = NULL;
    if (op->net_op_live)
    {
        op->net_op_live = false;
        mel_net_cancel(op->http->net, op->net_op);
    }
}

static void op_close_timer(Mel_Http_Op_Record* op)
{
    if (!op->timer)
        return;
    Mel_Vat_Source* t = op->timer;
    op->timer = NULL;
    mel_vat_source_close(t);
}

static void waiting_remove(Http_Bucket* bucket, Mel_Http_Op_Record* op)
{
    for (usize i = 0; i < bucket->waiting.count; i++)
    {
        if (bucket->waiting.items[i] == op)
        {
            mel_array_remove_ordered(&bucket->waiting, i);
            return;
        }
    }
}

static void fetch_settle(Mel_Http_Op_Record* op, Mel_Http_Status status, i32 os_error)
{
    if (op->settled)
        return;
    op->settled = true;

    op->result.status = status;
    op->result.os_error = os_error;

    op_stop_io(op);
    op_close_timer(op);

    if (op->waiting_in_bucket && op->bucket)
    {
        op->waiting_in_bucket = false;
        waiting_remove(op->bucket, op);
    }
    if (op->conn)
    {
        Http_Conn* c = op->conn;
        op->conn = NULL;
        conn_destroy(op->http, c);
    }

    if (!op->detached)
    {
        op->detached = true;
        mel_slotmap_remove(&op->http->ops, op->self);
    }

    if (status & MEL_HTTP_CANCELLED)
        mel_future_cancel(&op->future);
    else
        mel_future_resolve(&op->future, &op->result, http_future_status_from(status));
}

static void io_cont_run(Mel_Task* self)
{
    Mel_Http_Op_Record* op = mel_container_of(self, Mel_Http_Op_Record, io_task);
    Mel_Future*         f = op->io_future;
    op->io_future = NULL;
    Mel_IO_Result r = *mel_stream_future_result(f);
    mel_stream_future_release(f);

    void (*fn)(Mel_Http_Op_Record*, Mel_IO_Result) = op->on_io;
    op->on_io = NULL;
    if (!op->settled && fn)
        fn(op, r);
    op_unref(op);
}

static void io_submit(Mel_Http_Op_Record* op, Mel_Future* f, void (*fn)(Mel_Http_Op_Record*, Mel_IO_Result))
{
    if (!f)
    {
        fetch_settle(op, MEL_HTTP_ERROR, 0);
        return;
    }
    op->refs++;
    op->io_future = f;
    op->on_io = fn;
    mel_task_init(&op->io_task, io_cont_run);
    mel_future_then(f, &op->io_task, loop_exec(op));
}

static void net_cont_run(Mel_Task* self)
{
    Mel_Http_Op_Record* op = mel_container_of(self, Mel_Http_Op_Record, net_task);
    Mel_Future*         f = op->net_future;
    op->net_future = NULL;
    op->net_op_live = false;

    void (*fn)(Mel_Http_Op_Record*, Mel_Future*) = op->on_net;
    op->on_net = NULL;
    if (!op->settled && fn)
        fn(op, f);
    else
        mel_net_future_release(f);
    op_unref(op);
}

static void net_submit(Mel_Http_Op_Record* op, Mel_Future* f, void (*fn)(Mel_Http_Op_Record*, Mel_Future*))
{
    if (!f)
    {
        fetch_settle(op, MEL_HTTP_ERROR, 0);
        return;
    }
    op->refs++;
    op->net_future = f;
    op->on_net = fn;
    op->net_op_live = true;
    mel_task_init(&op->net_task, net_cont_run);
    mel_future_then(f, &op->net_task, loop_exec(op));
}

static i64 op_timer_deadline(Mel_Vat_Source* s)
{
    Mel_Http_Op_Record* op = (Mel_Http_Op_Record*)mel_vat_source_state(s);
    i64                 d = MEL_VAT_NEVER;
    if (op->deadline_total != 0 && op->deadline_total < d)
        d = op->deadline_total;
    if (op->deadline_response != 0 && op->deadline_response < d)
        d = op->deadline_response;
    return d;
}

static bool op_timer_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    Mel_Http_Op_Record* op = (Mel_Http_Op_Record*)mel_vat_source_state(s);
    if (op->settled)
        return false;
    i64 d = op_timer_deadline(s);
    if (d == MEL_VAT_NEVER || now_ns() < d)
        return false;
    fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_TIMED_OUT, 0);
    return false;
}

static const Mel_Vat_Source_Vtbl OP_TIMER_VT = {
    .wakeables = NULL,
    .deadline = op_timer_deadline,
    .drain = op_timer_drain,
    .cancel = NULL,
};

static void op_arm_timer(Mel_Http_Op_Record* op)
{
    if (op->timer)
    {
        mel_vat_source_demand_changed(op->timer);
        return;
    }
    if (op->deadline_total == 0 && op->deadline_response == 0)
        return;
    op->timer = mel_vat_source_open(mel_net_vat(op->http->net), &OP_TIMER_VT, op);
}

static Http_Bucket* bucket_get(Mel_Http* http, str8 host, u16 port)
{
    str8 key = str8_fmt_alloc(http->alloc, "%.*s:%u", (int)host.len, host.data, (u32)port);
    for (usize i = 0; i < http->buckets.count; i++)
    {
        if (str8_ieq(http->buckets.items[i]->key, key))
        {
            mel_dealloc(http->alloc, key.data);
            return http->buckets.items[i];
        }
    }
    Http_Bucket* b = mel_alloc_type(http->alloc, Http_Bucket);
    if (!b)
    {
        mel_dealloc(http->alloc, key.data);
        return NULL;
    }
    memset(b, 0, sizeof *b);
    b->key = key;
    mel_array_init(&b->idle, http->alloc);
    mel_array_init(&b->waiting, http->alloc);
    mel_array_push(&http->buckets, b);
    return b;
}

static void bucket_bind_idle(Mel_Http* http, Http_Bucket* bucket, Mel_Http_Op_Record* op)
{
    (void)http;
    Http_Conn* c = mel_array_pop(&bucket->idle);
    c->idle_since = 0;
    op->conn = c;
    op->reused = true;
    fetch_send(op);
}

static void fetch_on_connect(Mel_Http_Op_Record* op, Mel_Future* f)
{
    const Mel_Net_Conn_Result* r = mel_net_future_conn(f);
    Mel_Net_Status             st = r->status;
    i32                        os_err = r->os_error;
    Mel_Net_Conn*              nc = mel_net_future_take_conn(f);
    mel_net_future_release(f);

    if (!nc)
    {
        op->bucket->active--;
        bucket_pump(op->http, op->bucket);
        Mel_Http_Status hs = MEL_HTTP_ERROR | MEL_HTTP_CONNECT_FAILED;
        if (mel_net_status_timed_out(st))
            hs |= MEL_HTTP_TIMED_OUT;
        fetch_settle(op, hs, os_err);
        return;
    }

    Http_Conn* c = conn_wrap(op->http, nc, op->bucket);
    if (!c)
    {
        op->bucket->active--;
        bucket_pump(op->http, op->bucket);
        fetch_settle(op, MEL_HTTP_ERROR, 0);
        return;
    }
    op->conn = c;
    op->reused = false;
    fetch_send(op);
}

static void fetch_on_resolve(Mel_Http_Op_Record* op, Mel_Future* f)
{
    const Mel_Net_Resolve_Result* r = mel_net_future_resolve(f);
    if (!mel_net_status_ok(r->status) || r->count == 0)
    {
        i32 os_err = r->os_error;
        mel_net_future_release(f);
        op->bucket->active--;
        bucket_pump(op->http, op->bucket);
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_RESOLVE_FAILED, os_err);
        return;
    }
    Mel_Net_Address addr = r->items[0];
    mel_net_future_release(f);

    Mel_Net_Op  nop = MEL_NET_OP_NULL;
    Mel_Future* cf = mel_net_tcp_connect(op->http->net, .address = addr, .timeout_ns = op->connect_timeout, .nodelay = true, .out_op = &nop);
    op->net_op = nop;
    net_submit(op, cf, fetch_on_connect);
}

static void fetch_dial(Mel_Http_Op_Record* op)
{
    Mel_Net_Op  nop = MEL_NET_OP_NULL;
    Mel_Future* rf = mel_net_resolve(op->http->net, op->url.host, .port = mel_http_url_effective_port(&op->url), .out_op = &nop);
    op->net_op = nop;
    net_submit(op, rf, fetch_on_resolve);
}

static void bucket_pump(Mel_Http* http, Http_Bucket* bucket)
{
    while (bucket->waiting.count > 0)
    {
        if (bucket->idle.count > 0)
        {
            Mel_Http_Op_Record* op = bucket->waiting.items[0];
            mel_array_remove_ordered(&bucket->waiting, 0);
            op->waiting_in_bucket = false;
            bucket_bind_idle(http, bucket, op);
            continue;
        }
        if (bucket->active < http->max_conns_per_host)
        {
            Mel_Http_Op_Record* op = bucket->waiting.items[0];
            mel_array_remove_ordered(&bucket->waiting, 0);
            op->waiting_in_bucket = false;
            bucket->active++;
            fetch_dial(op);
            continue;
        }
        break;
    }
}

static void fetch_acquire(Mel_Http_Op_Record* op)
{
    str8 host = op->url.host;
    if (host.len >= 2 && host.data[0] == '[' && host.data[host.len - 1] == ']')
        host = str8_slice(host, 1, (size)host.len - 2);

    Http_Bucket* bucket = bucket_get(op->http, host, mel_http_url_effective_port(&op->url));
    if (!bucket)
    {
        fetch_settle(op, MEL_HTTP_ERROR, 0);
        return;
    }
    op->bucket = bucket;

    if (bucket->idle.count > 0)
    {
        bucket_bind_idle(op->http, bucket, op);
        return;
    }
    if (bucket->active < op->http->max_conns_per_host)
    {
        bucket->active++;
        fetch_dial(op);
        return;
    }
    op->waiting_in_bucket = true;
    mel_array_push(&bucket->waiting, op);
}

static void fetch_retry_fresh(Mel_Http_Op_Record* op)
{
    mel_log_debug("http", "pooled conn was dead at reuse; retrying on a fresh dial");
    op->retried = true;
    Http_Conn* c = op->conn;
    op->conn = NULL;
    if (op->parser_live)
    {
        mel_http__parser_free(&op->parser);
        op->parser_live = false;
    }
    mel_array_clear(&op->body_buf);
    mel_array_clear(&op->sink_staging);
    if (op->head_bytes.data)
    {
        mel_dealloc(op->alloc, op->head_bytes.data);
        op->head_bytes = STR8_EMPTY;
    }
    op->bucket->active++;
    conn_destroy(op->http, c);
    fetch_dial(op);
}

static bool fetch_can_retry(Mel_Http_Op_Record* op) { return op->reused && !op->retried && !op->response_bytes_seen; }

static void fetch_finish_response(Mel_Http_Op_Record* op);

static void on_body_collect(void* user, str8 span)
{
    Mel_Http_Op_Record* op = (Mel_Http_Op_Record*)user;
    if (op->sink)
    {
        for (usize i = 0; i < span.len; i++)
            mel_array_push(&op->sink_staging, span.data[i]);
        return;
    }
    if (op->max_body > 0 && op->body_buf.count + span.len > op->max_body)
    {
        op->body_error = MEL_HTTP_ERROR | MEL_HTTP_BODY_TOO_LARGE;
        return;
    }
    for (usize i = 0; i < span.len; i++)
        mel_array_push(&op->body_buf, span.data[i]);
}

static void fetch_on_sink_written(Mel_Http_Op_Record* op, Mel_IO_Result r)
{
    if (mel_io_status_failed(r.status))
    {
        if (op->conn)
        {
            Http_Conn* c = op->conn;
            op->conn = NULL;
            conn_destroy(op->http, c);
        }
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_SINK_FAILED, r.os_error);
        return;
    }
    mel_array_clear(&op->sink_staging);
    if (op->parser.done)
    {
        fetch_finish_response(op);
        return;
    }
    fetch_read_step(op);
}

static void fetch_flush_sink_or_continue(Mel_Http_Op_Record* op)
{
    if (op->sink && op->sink_staging.count > 0)
    {
        Mel_Future* f = mel_stream_write(op->sink, .buffer = op->sink_staging.items, .len = op->sink_staging.count);
        io_submit(op, f, fetch_on_sink_written);
        return;
    }
    if (op->parser.done)
    {
        fetch_finish_response(op);
        return;
    }
    fetch_read_step(op);
}

static void fetch_do_redirect(Mel_Http_Op_Record* op, str8 location)
{
    const Mel_Alloc* a = op->alloc;
    str8             next = STR8_EMPTY;

    bool absolute = str8_find(location, S8("://")) > 0;
    if (absolute)
    {
        next = str8_dup_alloc(location, a);
    }
    else if (location.len > 0 && location.data[0] == '/')
    {
        u16  port = mel_http_url_effective_port(&op->url);
        bool default_port = !op->url.port_explicit;
        if (default_port)
            next = str8_fmt_alloc(a, "%.*s://%.*s%.*s", (int)op->url.scheme.len, op->url.scheme.data, (int)op->url.host.len, op->url.host.data, (int)location.len, location.data);
        else
            next = str8_fmt_alloc(a, "%.*s://%.*s:%u%.*s", (int)op->url.scheme.len, op->url.scheme.data, (int)op->url.host.len, op->url.host.data, (u32)port, (int)location.len, location.data);
    }
    else
    {
        mel_log_warn("http", "redirect with relative-path Location is not supported");
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_MALFORMED, 0);
        return;
    }

    Mel_Http_Url next_url;
    if (!mel_http_status_ok(mel_http_url_parse(next, &next_url)))
    {
        mel_dealloc(a, next.data);
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_MALFORMED, 0);
        return;
    }

    bool was_https = str8_ieq_cstr(op->url.scheme, "https");
    bool is_http = str8_ieq_cstr(next_url.scheme, "http");
    if (was_https && is_http && !op->allow_insecure_redirect)
    {
        mel_dealloc(a, next.data);
        mel_log_warn("http", "refusing https -> http redirect; set allow_insecure_redirect to permit");
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_TOO_MANY_REDIRECTS, 0);
        return;
    }

    i32  code = op->parser.status_code;
    bool to_get = code == 303 || ((code == 301 || code == 302) && !str8_ieq_cstr(op->method, "GET") && !str8_ieq_cstr(op->method, "HEAD"));
    if (to_get)
    {
        mel_dealloc(a, op->method.data);
        op->method = str8_dup_alloc(S8("GET"), a);
        if (op->body.data)
        {
            mel_dealloc(a, op->body.data);
            op->body = STR8_EMPTY;
        }
    }

    mel_dealloc(a, op->url_text.data);
    op->url_text = next;
    if (!mel_http_status_ok(mel_http_url_parse(op->url_text, &op->url)))
    {
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_MALFORMED, 0);
        return;
    }

    if (op->parser_live)
    {
        mel_http__parser_free(&op->parser);
        op->parser_live = false;
    }
    mel_array_clear(&op->body_buf);
    mel_array_clear(&op->sink_staging);
    op->redirects_left--;
    op->reused = false;
    op->retried = false;
    op->response_bytes_seen = false;
    op->deadline_response = 0;

    fetch_acquire(op);
}

static void fetch_finish_response(Mel_Http_Op_Record* op)
{
    Http_Conn* c = op->conn;
    op->conn = NULL;
    if (c)
    {
        if (mel_http__parser_keep_alive(&op->parser))
            conn_pool_return(op->http, c);
        else
            conn_destroy(op->http, c);
    }

    if (op->body_error != 0)
    {
        fetch_settle(op, op->body_error, 0);
        return;
    }

    i32 code = op->parser.status_code;
    if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && op->redirects_left > 0)
    {
        str8 location = STR8_EMPTY;
        bool has_location = false;
        for (usize i = 0; i < op->parser.headers.count; i++)
        {
            if (mel_http__header_ieq(op->parser.headers.items[i].name, "location"))
            {
                location = op->parser.headers.items[i].value;
                has_location = true;
                break;
            }
        }
        if (has_location)
        {
            str8 loc = str8_dup_alloc(location, op->alloc);
            fetch_do_redirect(op, loc);
            if (loc.data)
                mel_dealloc(op->alloc, loc.data);
            return;
        }
    }

    op->result.status_code = op->parser.status_code;
    op->result.reason = op->parser.reason;
    op->result.headers = op->parser.headers.items;
    op->result.header_count = op->parser.headers.count;
    op->result.body = (str8){ op->body_buf.items, (size)op->body_buf.count };
    fetch_settle(op, MEL_HTTP_OK, 0);
}

static void fetch_on_read(Mel_Http_Op_Record* op, Mel_IO_Result r)
{
    if (mel_io_status_failed(r.status))
    {
        if (fetch_can_retry(op))
        {
            fetch_retry_fresh(op);
            return;
        }
        Http_Conn* c = op->conn;
        op->conn = NULL;
        conn_destroy(op->http, c);
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_BODY_INCOMPLETE, r.os_error);
        return;
    }

    if (r.bytes_transferred > 0)
    {
        op->response_bytes_seen = true;
        usize           consumed = 0;
        Mel_Http_Status st = mel_http__parser_feed(&op->parser, op->read_buf, r.bytes_transferred, &consumed, on_body_collect, op);
        if (!mel_http_status_ok(st))
        {
            Http_Conn* c = op->conn;
            op->conn = NULL;
            conn_destroy(op->http, c);
            fetch_settle(op, st, 0);
            return;
        }
        if (op->body_error != 0)
        {
            Http_Conn* c = op->conn;
            op->conn = NULL;
            conn_destroy(op->http, c);
            fetch_settle(op, op->body_error, 0);
            return;
        }
        fetch_flush_sink_or_continue(op);
        return;
    }

    if (r.status & MEL_IO_EOF)
    {
        if (!op->response_bytes_seen && fetch_can_retry(op))
        {
            fetch_retry_fresh(op);
            return;
        }
        Mel_Http_Status st = mel_http__parser_finish_eof(&op->parser);
        Http_Conn*      c = op->conn;
        op->conn = NULL;
        conn_destroy(op->http, c);
        if (!mel_http_status_ok(st))
        {
            op->result.status_code = op->parser.status_code;
            op->result.reason = op->parser.reason;
            op->result.headers = op->parser.headers.items;
            op->result.header_count = op->parser.headers.count;
            op->result.body = (str8){ op->body_buf.items, (size)op->body_buf.count };
            fetch_settle(op, st, 0);
            return;
        }
        fetch_flush_sink_or_continue(op);
        return;
    }

    fetch_read_step(op);
}

static void fetch_read_step(Mel_Http_Op_Record* op)
{
    Mel_Future* f = mel_stream_read(mel_net_conn_stream(op->conn->conn), .buffer = op->read_buf, .len = MEL_HTTP__READ_CHUNK);
    io_submit(op, f, fetch_on_read);
}

static void fetch_on_body_written(Mel_Http_Op_Record* op, Mel_IO_Result r)
{
    if (mel_io_status_failed(r.status))
    {
        if (fetch_can_retry(op))
        {
            fetch_retry_fresh(op);
            return;
        }
        Http_Conn* c = op->conn;
        op->conn = NULL;
        conn_destroy(op->http, c);
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_CONNECT_FAILED, r.os_error);
        return;
    }
    if (op->response_timeout != 0)
    {
        op->deadline_response = now_ns() + op->response_timeout;
        op_arm_timer(op);
    }
    fetch_read_step(op);
}

static void fetch_on_head_written(Mel_Http_Op_Record* op, Mel_IO_Result r)
{
    if (op->head_bytes.data)
    {
        mel_dealloc(op->alloc, op->head_bytes.data);
        op->head_bytes = STR8_EMPTY;
    }
    if (mel_io_status_failed(r.status))
    {
        if (fetch_can_retry(op))
        {
            fetch_retry_fresh(op);
            return;
        }
        Http_Conn* c = op->conn;
        op->conn = NULL;
        conn_destroy(op->http, c);
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_CONNECT_FAILED, r.os_error);
        return;
    }
    if (op->body.len > 0)
    {
        Mel_Future* f = mel_stream_write(mel_net_conn_stream(op->conn->conn), .buffer = op->body.data, .len = (usize)op->body.len);
        io_submit(op, f, fetch_on_body_written);
        return;
    }
    fetch_on_body_written(op, (Mel_IO_Result){ .status = MEL_IO_OK });
}

static void fetch_send(Mel_Http_Op_Record* op)
{
    str8 host = op->url.host;
    u16  port = mel_http_url_effective_port(&op->url);
    bool default_port = !op->url.port_explicit || port == 80;

    str8 target = mel_http_url_target(&op->url, op->alloc);
    op->head_bytes = mel_http__wire_request_head(op->alloc, op->method, target, host, port, default_port, op->req_headers, op->req_header_count, op->body.len > 0 ? (i64)op->body.len : 0);
    mel_dealloc(op->alloc, target.data);

    mel_http__parser_init(&op->parser, op->alloc, op->http->max_head, str8_ieq_cstr(op->method, "HEAD"));
    op->parser_live = true;
    op->body_error = 0;

    Mel_Future* f = mel_stream_write(mel_net_conn_stream(op->conn->conn), .buffer = op->head_bytes.data, .len = (usize)op->head_bytes.len);
    io_submit(op, f, fetch_on_head_written);
}

void mel_http_destroy(Mel_Http* http)
{
    if (!http)
        return;
    assert(mel_vat_is_owner(mel_net_vat(http->net)));
    http->destroying = true;

    Mel_Array(Mel_Http_Op_Record*) snap;
    mel_array_init(&snap, http->alloc);
    Mel_Http_Op_Record** data = (Mel_Http_Op_Record**)mel_slotmap_data(&http->ops);
    u32                  n = mel_slotmap_count(&http->ops);
    for (u32 i = 0; i < n; i++)
        mel_array_push(&snap, data[i]);
    for (usize i = 0; i < snap.count; i++)
        fetch_settle(snap.items[i], MEL_HTTP_ERROR | MEL_HTTP_CANCELLED, 0);
    mel_array_free(&snap);

    if (http->sweep)
    {
        mel_vat_source_close(http->sweep);
        http->sweep = NULL;
    }

    for (usize i = 0; i < http->buckets.count; i++)
    {
        Http_Bucket* b = http->buckets.items[i];
        for (usize k = 0; k < b->idle.count; k++)
        {
            mel_net_conn_destroy(b->idle.items[k]->conn);
            mel_dealloc(http->alloc, b->idle.items[k]);
        }
        mel_array_free(&b->idle);
        mel_array_free(&b->waiting);
        mel_dealloc(http->alloc, b->key.data);
        mel_dealloc(http->alloc, b);
    }
    mel_array_free(&http->buckets);
    mel_slotmap_free(&http->ops);
    mel_dealloc(http->alloc, http);
}

static i64 sweep_deadline(Mel_Vat_Source* s)
{
    Mel_Http* http = (Mel_Http*)mel_vat_source_state(s);
    i64       min = MEL_VAT_NEVER;
    for (usize i = 0; i < http->buckets.count; i++)
    {
        Http_Bucket* b = http->buckets.items[i];
        for (usize k = 0; k < b->idle.count; k++)
        {
            i64 d = b->idle.items[k]->idle_since + http->idle_timeout;
            if (d < min)
                min = d;
        }
    }
    return min;
}

static bool sweep_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    Mel_Http* http = (Mel_Http*)mel_vat_source_state(s);
    i64       now = now_ns();
    for (usize i = 0; i < http->buckets.count; i++)
    {
        Http_Bucket* b = http->buckets.items[i];
        for (usize k = 0; k < b->idle.count;)
        {
            Http_Conn* c = b->idle.items[k];
            if (c->idle_since + http->idle_timeout <= now)
            {
                mel_array_remove_unordered(&b->idle, k);
                conn_destroy(http, c);
                continue;
            }
            k++;
        }
    }
    return false;
}

static const Mel_Vat_Source_Vtbl SWEEP_VT = {
    .wakeables = NULL,
    .deadline = sweep_deadline,
    .drain = sweep_drain,
    .cancel = NULL,
};

Mel_Http* mel_http_create_opt(Mel_Http_Opt opt)
{
    if (!opt.net)
    {
        mel_log_error("http", "create: net is required");
        return NULL;
    }
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Http*        http = mel_alloc_type(alloc, Mel_Http);
    if (!http)
        return NULL;
    memset(http, 0, sizeof *http);
    http->net = opt.net;
    http->alloc = alloc;

    http->max_conns_per_host = opt.max_conns_per_host;
    if (http->max_conns_per_host == 0)
    {
        http->max_conns_per_host = 4;
        mel_log_info("http", "create: max_conns_per_host not set; using 4");
    }
    http->idle_timeout = opt.pool_idle_timeout_ns;
    if (http->idle_timeout == 0)
        mel_log_info("http", "create: pool_idle_timeout_ns not set; idle connections are kept until destroy");
    http->max_head = opt.max_header_bytes;
    if (http->max_head == 0)
    {
        http->max_head = 64 * 1024;
        mel_log_info("http", "create: max_header_bytes not set; using 64 KiB");
    }

    mel_slotmap_init(&http->ops, alloc, .item_size = sizeof(Mel_Http_Op_Record*), .initial_capacity = 16);
    mel_array_init(&http->buckets, alloc);

    if (http->idle_timeout > 0)
        http->sweep = mel_vat_source_open(mel_net_vat(http->net), &SWEEP_VT, http);

    return http;
}

bool     mel_http_available(const Mel_Http* http) { return http && mel_net_available(http->net); }
Mel_Net* mel_http_net(const Mel_Http* http) { return http ? http->net : NULL; }
u32      mel_http_pending(const Mel_Http* http) { return http ? mel_slotmap_count((Mel_SlotMap*)&http->ops) : 0; }

bool mel_http_cancel(Mel_Http* http, Mel_Http_Op handle)
{
    if (!http)
        return false;
    assert(mel_vat_is_owner(mel_net_vat(http->net)));
    Mel_SlotMap_Handle   h = mel_slotmap_handle_make(handle.index, handle.generation);
    Mel_Http_Op_Record** pp = (Mel_Http_Op_Record**)mel_slotmap_get(&http->ops, h);
    Mel_Http_Op_Record*  op = pp ? *pp : NULL;
    if (!op || op->settled)
        return false;
    fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_CANCELLED, 0);
    return true;
}

static str8 dup_or_empty(str8 s, const Mel_Alloc* a) { return s.len > 0 ? str8_dup_alloc(s, a) : STR8_EMPTY; }

Mel_Future* mel_http_fetch_opt(Mel_Http* http, Mel_Http_Request req, Mel_Http_Fetch_Opt opt)
{
    if (!http)
        return NULL;
    assert(mel_vat_is_owner(mel_net_vat(http->net)));

    const Mel_Alloc*    alloc = http->alloc;
    Mel_Http_Op_Record* op = mel_alloc_type(alloc, Mel_Http_Op_Record);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);

    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;
    op->http = http;
    op->alloc = alloc;
    op->deliver = opt.deliver;
    mel_array_init(&op->body_buf, alloc);
    mel_array_init(&op->sink_staging, alloc);

    Mel_Http_Op_Record* slot = op;
    op->self = mel_slotmap_insert(&http->ops, &slot);
    if (opt.out_op)
        *opt.out_op = (Mel_Http_Op){ .index = op->self.index, .generation = op->self.generation };

    if (!mel_net_available(http->net))
    {
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_UNAVAILABLE, 0);
        return &op->future;
    }
    if (req.body_stream)
    {
        mel_log_error("http", "fetch: body_stream is not supported yet; pass body bytes");
        assert(!req.body_stream);
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_UNAVAILABLE, 0);
        return &op->future;
    }
    op->method = dup_or_empty(req.method.len > 0 ? req.method : S8("GET"), alloc);
    op->url_text = dup_or_empty(req.url, alloc);
    op->body = dup_or_empty(req.body, alloc);
    op->sink = opt.sink;
    if (req.header_count > 0)
    {
        op->req_headers = mel_alloc_array(alloc, Mel_Http_Header, req.header_count);
        if (!op->req_headers)
        {
            fetch_settle(op, MEL_HTTP_ERROR, 0);
            return &op->future;
        }
        memset(op->req_headers, 0, sizeof(Mel_Http_Header) * req.header_count);
        op->req_header_count = req.header_count;
        for (usize i = 0; i < req.header_count; i++)
        {
            op->req_headers[i].name = dup_or_empty(req.headers[i].name, alloc);
            op->req_headers[i].value = dup_or_empty(req.headers[i].value, alloc);
        }
    }

    op->connect_timeout = opt.connect_timeout_ns;
    op->response_timeout = opt.response_timeout_ns;
    op->total_timeout = opt.total_timeout_ns;
    op->redirects_left = opt.max_redirects;
    op->allow_insecure_redirect = opt.allow_insecure_redirect;
    op->max_body = opt.max_body_bytes;

    if (opt.connect_timeout_ns == 0 && opt.response_timeout_ns == 0 && opt.total_timeout_ns == 0 && !http->zero_timeout_logged)
    {
        http->zero_timeout_logged = true;
        mel_log_warn("http", "fetch: no timeout set; a silent peer will stall this fetch forever");
    }

    op->read_buf = mel_alloc(alloc, MEL_HTTP__READ_CHUNK);
    if (!op->read_buf)
    {
        fetch_settle(op, MEL_HTTP_ERROR, 0);
        return &op->future;
    }

    if (!mel_http_status_ok(mel_http_url_parse(op->url_text, &op->url)))
    {
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_BAD_URL, 0);
        return &op->future;
    }
    if (str8_ieq_cstr(op->url.scheme, "https"))
    {
        mel_log_error("http", "fetch: https is not available yet (tls stream pending)");
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_UNAVAILABLE | MEL_HTTP_TLS_FAILED, 0);
        return &op->future;
    }
    if (!str8_ieq_cstr(op->url.scheme, "http"))
    {
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_BAD_URL, 0);
        return &op->future;
    }
    if (mel_http_url_effective_port(&op->url) == 0)
    {
        fetch_settle(op, MEL_HTTP_ERROR | MEL_HTTP_BAD_URL, 0);
        return &op->future;
    }

    if (op->total_timeout != 0)
    {
        op->deadline_total = now_ns() + op->total_timeout;
        op_arm_timer(op);
    }

    fetch_acquire(op);
    return &op->future;
}

const Mel_Http_Result* mel_http_future_result(Mel_Future* f)
{
    if (!f)
        return NULL;
    Mel_Http_Op_Record* op = mel_container_of(f, Mel_Http_Op_Record, future);
    return &op->result;
}

str8 mel_http_future_take_body(Mel_Future* f)
{
    if (!f)
        return STR8_EMPTY;
    Mel_Http_Op_Record* op = mel_container_of(f, Mel_Http_Op_Record, future);
    str8                body = { op->body_buf.items, (size)op->body_buf.count };
    op->body_buf.items = NULL;
    op->body_buf.count = 0;
    op->body_buf.capacity = 0;
    op->result.body = STR8_EMPTY;
    return body;
}

void mel_http_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Mel_Http_Op_Record* op = mel_container_of(f, Mel_Http_Op_Record, future);
    op->released = true;
    if (op->refs == 0)
        op_free(op);
}

bool mel_http_result_header(const Mel_Http_Result* r, str8 name, str8* out_value)
{
    if (!r)
        return false;
    for (usize i = 0; i < r->header_count; i++)
    {
        if (str8_ieq(r->headers[i].name, name))
        {
            if (out_value)
                *out_value = r->headers[i].value;
            return true;
        }
    }
    return false;
}
