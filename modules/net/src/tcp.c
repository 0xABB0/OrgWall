#include "net_internal.h"
#include "net_backend.h"

#include <allocator/allocator.h>
#include <collection/list.h>
#include <io/stream.h>
#include <port/port.h>
#include <log/log.h>
#include <time/nano.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct
{
    Mel_Future       future;
    Mel_IO_Result    result;
    const Mel_Alloc* alloc;
    bool             owned;
} Conn_Stream_Op;

static_assert(offsetof(Conn_Stream_Op, future) == 0, "io stream-op contract: the record must mirror Mel_IO_Op (future, result, alloc, owned) so mel_stream_future_release recovers it");

typedef struct
{
    i32              fd;
    Mel_Port*        port;
    const Mel_Alloc* alloc;
} Conn_Stream_State;

typedef struct
{
    Mel_Task        task;
    Mel_Future*     port_future;
    Conn_Stream_Op* op;
} Conn_Stream_Cont;

static Mel_Future_Status conn_stream_future_status_from(Mel_IO_Status status)
{
    Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
    if (status & MEL_IO_PARTIAL)
        fs |= MEL_FUTURE_PARTIAL;
    if (status & MEL_IO_CANCELLED)
        fs |= MEL_FUTURE_CANCELLED;
    return fs;
}

static Mel_IO_Status conn_stream_status_from_port(Mel_Port_Status ps)
{
    Mel_IO_Status st = ps & MEL_IO_SEVERITY_MASK;
    if (ps & MEL_PORT_CANCELLED)
        st |= MEL_IO_CANCELLED;
    if (ps & MEL_PORT_EOF)
        st |= MEL_IO_EOF;
    if (ps & MEL_PORT_PARTIAL)
        st |= MEL_IO_PARTIAL;
    if (ps & MEL_PORT_PEER_CLOSE)
        st |= MEL_IO_PEER_CLOSE;
    if (ps & MEL_PORT_BAD_FD)
        st |= MEL_IO_BAD_HANDLE;
    if (ps & MEL_PORT_UNAVAILABLE)
        st |= MEL_IO_UNAVAILABLE;
    return st;
}

static Conn_Stream_Op* conn_stream_op_new(const Mel_Alloc* alloc)
{
    Conn_Stream_Op* op = mel_alloc_type(alloc, Conn_Stream_Op);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);
    op->alloc = alloc;
    op->owned = true;
    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;
    return op;
}

static Mel_Future* conn_stream_op_resolve(Conn_Stream_Op* op, usize bytes, i32 os_error, Mel_IO_Status status)
{
    op->result.bytes_transferred = bytes;
    op->result.position = 0;
    op->result.os_error = os_error;
    op->result.status = status;
    if (status & MEL_IO_CANCELLED)
        mel_future_cancel(&op->future);
    else
        mel_future_resolve(&op->future, &op->result, conn_stream_future_status_from(status));
    return &op->future;
}

static void conn_stream_cont_run(Mel_Task* self)
{
    Conn_Stream_Cont*      k = mel_container_of(self, Conn_Stream_Cont, task);
    const Mel_Port_Result* pr = mel_port_future_result(k->port_future);
    usize                  bytes = pr ? pr->bytes_transferred : 0;
    i32                    os_err = pr ? pr->os_error : 0;
    Mel_IO_Status          st = pr ? conn_stream_status_from_port(pr->status) : (MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    mel_port_future_release(k->port_future);

    Conn_Stream_Op*  op = k->op;
    const Mel_Alloc* alloc = op->alloc;
    conn_stream_op_resolve(op, bytes, os_err, st);
    mel_dealloc(alloc, k);
}

static Mel_Future* conn_stream_submit(Conn_Stream_State* f, bool is_read, void* buf, usize len, Mel_Executor* deliver)
{
    Conn_Stream_Op* op = conn_stream_op_new(f->alloc);
    if (!op)
        return NULL;

    if (!f->port || !mel_port_available(f->port))
        return conn_stream_op_resolve(op, 0, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    assert(mel_vat_is_owner(mel_port_vat(f->port)));

    Mel_Port_Op port_op = MEL_PORT_OP_NULL;
    Mel_Future* pf;
    if (is_read)
        pf = mel_port_read_opt(f->port, (Mel_Port_Read_Opt){ .fd = f->fd, .buffer = buf, .len = len, .offset = MEL_PORT_NO_OFFSET, .deliver = deliver, .out_op = &port_op });
    else
        pf = mel_port_write_opt(f->port, (Mel_Port_Write_Opt){ .fd = f->fd, .buffer = buf, .len = len, .offset = MEL_PORT_NO_OFFSET, .deliver = deliver, .out_op = &port_op });

    if (!pf)
        return conn_stream_op_resolve(op, 0, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    Conn_Stream_Cont* k = mel_alloc_type(f->alloc, Conn_Stream_Cont);
    if (!k)
    {
        mel_port_cancel(f->port, port_op);
        mel_port_future_release(pf);
        return conn_stream_op_resolve(op, 0, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    }
    memset(k, 0, sizeof *k);
    k->port_future = pf;
    k->op = op;
    mel_task_init(&k->task, conn_stream_cont_run);
    mel_future_then(pf, &k->task, deliver ? deliver : mel_port_executor(f->port));
    return &op->future;
}

static Mel_Future* conn_stream_read(Mel_Stream* s, void* user, Mel_Stream_Read_Opt opt)
{
    (void)s;
    return conn_stream_submit((Conn_Stream_State*)user, true, opt.buffer, opt.len, opt.deliver);
}

static Mel_Future* conn_stream_write(Mel_Stream* s, void* user, Mel_Stream_Write_Opt opt)
{
    (void)s;
    return conn_stream_submit((Conn_Stream_State*)user, false, (void*)opt.buffer, opt.len, opt.deliver);
}

static void conn_stream_close(Mel_Stream* s, void* user)
{
    Conn_Stream_State* f = (Conn_Stream_State*)user;
    if (f->fd >= 0)
        mel_net__backend_close(f->fd);
    if (f->port)
        mel_port_destroy(f->port);
    mel_dealloc(mel_stream_alloc(s), f);
}

static const Mel_Stream_Iface NET_CONN_STREAM_IFACE = {
    .name = "net-conn",
    .read = conn_stream_read,
    .write = conn_stream_write,
    .close = conn_stream_close,
};

Mel_Net_Conn* mel_net__conn_create(Mel_Net* net, i32 fd, const Mel_Alloc* alloc)
{
    Mel_Net_Conn* conn = mel_alloc_type(alloc, Mel_Net_Conn);
    if (!conn)
    {
        mel_net__backend_close(fd);
        return NULL;
    }
    memset(conn, 0, sizeof *conn);
    conn->net = net;
    conn->alloc = alloc;
    conn->fd = fd;
    mel_net__backend_local_address(fd, &conn->local);
    mel_net__backend_peer_address(fd, &conn->peer);

    Conn_Stream_State* f = mel_alloc_type(alloc, Conn_Stream_State);
    if (!f)
    {
        mel_net__backend_close(fd);
        mel_dealloc(alloc, conn);
        return NULL;
    }
    memset(f, 0, sizeof *f);
    f->fd = fd;
    f->alloc = alloc;
    f->port = mel_port_create(.vat = net->vat, .alloc = alloc);

    bool async = f->port && mel_port_available(f->port);

    conn->stream = mel_stream_create(.iface = &NET_CONN_STREAM_IFACE, .user = f, .alloc = alloc, .vat = net->vat, .executor = net->executor, .caps = { .readable = true, .writable = true, .async = async });
    if (!conn->stream)
    {
        if (f->port)
            mel_port_destroy(f->port);
        mel_net__backend_close(fd);
        mel_dealloc(alloc, f);
        mel_dealloc(alloc, conn);
        return NULL;
    }
    return conn;
}

Mel_Stream*     mel_net_conn_stream(Mel_Net_Conn* conn) { return conn ? conn->stream : NULL; }
Mel_Net_Address mel_net_conn_local_address(const Mel_Net_Conn* conn) { return conn->local; }
Mel_Net_Address mel_net_conn_peer_address(const Mel_Net_Conn* conn) { return conn->peer; }

Mel_Net_Status mel_net_conn_shutdown(Mel_Net_Conn* conn, bool read, bool write)
{
    if (!conn || conn->fd < 0)
        return MEL_NET_ERROR | MEL_NET_CLOSED;
    mel_net__backend_shutdown(conn->fd, read, write);
    return MEL_NET_OK;
}

void mel_net_conn_destroy(Mel_Net_Conn* conn)
{
    if (!conn)
        return;
    mel_stream_destroy(conn->stream);
    mel_dealloc(conn->alloc, conn);
}

static void conn_payload_free(Mel_Net_Op_Record* op)
{
    if (op->result.conn.conn)
    {
        mel_net_conn_destroy(op->result.conn.conn);
        op->result.conn.conn = NULL;
    }
}

static bool connect_on_event(Mel_Net_Op_Record* op, bool timed_out)
{
    if (timed_out)
    {
        mel_net__op_settle(op, MEL_NET_ERROR | MEL_NET_TIMED_OUT, 0);
        return false;
    }

    i32 err = mel_net__backend_connect_finish(op->fd);
    if (err == MEL_NET__WOULD_BLOCK)
        return true;
    if (err == 0)
    {
        i32 fd = op->fd;
        op->fd = -1;
        op->owns_fd = false;
        op->result.conn.conn = mel_net__conn_create(op->net, fd, op->alloc);
        if (!op->result.conn.conn)
        {
            mel_net__op_settle(op, MEL_NET_ERROR, 0);
            return false;
        }
        mel_net__op_settle(op, MEL_NET_OK, 0);
        return false;
    }
    mel_net__op_settle(op, mel_net__backend_status_from_os(err), err);
    return false;
}

Mel_Future* mel_net_tcp_connect_opt(Mel_Net* net, Mel_Net_Tcp_Connect_Opt opt)
{
    if (!net)
        return NULL;
    assert(mel_vat_is_owner(net->vat));

    Mel_Net_Op_Record* op = mel_net__op_begin(net, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->status_slot = &op->result.conn.status;
    op->oserr_slot = &op->result.conn.os_error;
    op->free_payload = conn_payload_free;
    op->on_event = connect_on_event;

    if (!net->backend_ready)
    {
        mel_log_error("net", "connect: no socket backend available");
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_UNAVAILABLE, 0);
    }

    if (opt.timeout_ns == 0 && !net->zero_timeout_logged)
    {
        net->zero_timeout_logged = true;
        mel_log_warn("net", "connect: timeout_ns is zero; a hung connect will wait forever");
    }

    Mel_Net__Connect_R r = mel_net__backend_connect_begin(&opt.address, opt.nodelay);
    if (r.fd < 0)
        return mel_net__op_fail(net, op, mel_net__backend_status_from_os(r.os_error), r.os_error);

    op->fd = r.fd;
    op->owns_fd = true;

    if (!r.pending)
    {
        connect_on_event(op, false);
        return &op->future;
    }

    i64 deadline = opt.timeout_ns > 0 ? (i64)mel_nanos_since_unspecified_epoch() + opt.timeout_ns : MEL_VAT_NEVER;
    mel_net__op_arm(op, r.fd, MEL_VAT_WAKE_OUT, deadline);
    return &op->future;
}

const Mel_Net_Conn_Result* mel_net_future_conn(Mel_Future* f)
{
    if (!f)
        return NULL;
    Mel_Net_Op_Record* op = mel_container_of(f, Mel_Net_Op_Record, future);
    return &op->result.conn;
}

Mel_Net_Conn* mel_net_future_take_conn(Mel_Future* f)
{
    if (!f)
        return NULL;
    Mel_Net_Op_Record* op = mel_container_of(f, Mel_Net_Op_Record, future);
    Mel_Net_Conn*      conn = op->result.conn.conn;
    op->result.conn.conn = NULL;
    return conn;
}

Mel_Net_Listener_Result mel_net_tcp_listen_opt(Mel_Net* net, Mel_Net_Tcp_Listen_Opt opt)
{
    Mel_Net_Listener_Result r = { 0 };
    if (!net)
    {
        r.status = MEL_NET_ERROR | MEL_NET_CLOSED;
        return r;
    }
    assert(mel_vat_is_owner(net->vat));
    if (!net->backend_ready)
    {
        mel_log_error("net", "listen: no socket backend available");
        r.status = MEL_NET_ERROR | MEL_NET_UNAVAILABLE;
        return r;
    }

    u32 backlog = opt.backlog;
    if (backlog == 0)
    {
        backlog = 64;
        mel_log_info("net", "listen: backlog not set; using 64");
    }

    i32 fd = -1;
    i32 err = mel_net__backend_listen(&opt.address, backlog, opt.reuse_addr, opt.v6_only, &fd);
    if (err != 0)
    {
        r.status = mel_net__backend_status_from_os(err);
        r.os_error = err;
        return r;
    }

    Mel_Net_Listener* l = mel_alloc_type(net->alloc, Mel_Net_Listener);
    if (!l)
    {
        mel_net__backend_close(fd);
        r.status = MEL_NET_ERROR;
        return r;
    }
    memset(l, 0, sizeof *l);
    l->net = net;
    l->alloc = net->alloc;
    l->fd = fd;
    mel_net__backend_local_address(fd, &l->bound);

    r.value = l;
    r.status = MEL_NET_OK;
    return r;
}

static void accept_on_settle(Mel_Net_Op_Record* op)
{
    if (op->listener && op->listener->pending_accept == op)
        op->listener->pending_accept = NULL;
}

static bool accept_on_event(Mel_Net_Op_Record* op, bool timed_out)
{
    (void)timed_out;
    i32             fd = -1;
    Mel_Net_Address peer = { 0 };
    i32             err = mel_net__backend_accept(op->listener->fd, &fd, &peer);
    if (err == MEL_NET__WOULD_BLOCK)
        return true;
    if (err != 0)
    {
        mel_net__op_settle(op, mel_net__backend_status_from_os(err), err);
        return false;
    }
    op->result.conn.conn = mel_net__conn_create(op->net, fd, op->alloc);
    if (!op->result.conn.conn)
    {
        mel_net__op_settle(op, MEL_NET_ERROR, 0);
        return false;
    }
    mel_net__op_settle(op, MEL_NET_OK, 0);
    return false;
}

Mel_Future* mel_net_listener_accept_opt(Mel_Net_Listener* listener, Mel_Net_Accept_Opt opt)
{
    if (!listener)
        return NULL;
    Mel_Net* net = listener->net;
    assert(mel_vat_is_owner(net->vat));

    Mel_Net_Op_Record* op = mel_net__op_begin(net, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->status_slot = &op->result.conn.status;
    op->oserr_slot = &op->result.conn.os_error;
    op->free_payload = conn_payload_free;
    op->on_event = accept_on_event;
    op->on_settle = accept_on_settle;
    op->listener = listener;

    if (listener->fd < 0)
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_CLOSED, 0);
    if (listener->pending_accept)
    {
        mel_log_error("net", "accept: an accept is already pending on this listener");
        assert(!listener->pending_accept);
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_BUSY, 0);
    }

    listener->pending_accept = op;
    mel_net__op_arm(op, listener->fd, MEL_VAT_WAKE_IN, MEL_VAT_NEVER);
    return &op->future;
}

Mel_Net_Address mel_net_listener_address(const Mel_Net_Listener* listener) { return listener->bound; }

void mel_net_listener_destroy(Mel_Net_Listener* listener)
{
    if (!listener)
        return;
    assert(mel_vat_is_owner(listener->net->vat));
    if (listener->pending_accept)
        mel_net__op_settle(listener->pending_accept, MEL_NET_ERROR | MEL_NET_CANCELLED, 0);
    if (listener->fd >= 0)
    {
        mel_net__backend_close(listener->fd);
        listener->fd = -1;
    }
    mel_dealloc(listener->alloc, listener);
}
