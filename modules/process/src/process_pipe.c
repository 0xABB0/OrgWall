#include "process_pipe.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <executor/executor.h>
#include <reactor/reactor.h>
#include <port/port.h>
#include <collection/list.h>
#include <log/log.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
    Mel_Future       future;
    Mel_IO_Result    result;
    const Mel_Alloc* alloc;
    bool             owned;
} Pipe_Op;

static_assert(offsetof(Pipe_Op, future) == 0, "io stream-op contract: the embedded future must lead the record so mel_stream_future_release(mel_container_of(f, op, future)) recovers the op");

typedef struct
{
    i32              fd;
    Mel_Port*        port;
    bool             readable;
    bool             writable;
    const Mel_Alloc* alloc;
} Pipe_State;

typedef struct
{
    Mel_Task    task;
    Mel_Future* port_future;
    Pipe_Op*    op;
    Mel_Stream* stream;
} Pipe_Cont;

static Mel_Future_Status pipe_future_status_from(Mel_IO_Status status)
{
    Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
    if (status & MEL_IO_PARTIAL)
        fs |= MEL_FUTURE_PARTIAL;
    if (status & MEL_IO_CANCELLED)
        fs |= MEL_FUTURE_CANCELLED;
    return fs;
}

static Mel_IO_Status pipe_status_from_port(Mel_Port_Status ps)
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

static Pipe_Op* pipe_op_new(const Mel_Alloc* alloc)
{
    Pipe_Op* op = mel_alloc_type(alloc, Pipe_Op);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);
    op->alloc = alloc;
    op->owned = true;
    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;
    return op;
}

static Mel_Future* pipe_op_resolve(Pipe_Op* op, usize bytes, i32 os_error, Mel_IO_Status status)
{
    op->result.bytes_transferred = bytes;
    op->result.position = 0;
    op->result.os_error = os_error;
    op->result.status = status;
    if (status & MEL_IO_CANCELLED)
        mel_future_cancel(&op->future);
    else
        mel_future_resolve(&op->future, &op->result, pipe_future_status_from(status));
    return &op->future;
}

static void pipe_cont_run(Mel_Task* self)
{
    Pipe_Cont*             k = mel_container_of(self, Pipe_Cont, task);
    const Mel_Port_Result* pr = mel_port_future_result(k->port_future);
    usize                  bytes = pr ? pr->bytes_transferred : 0;
    i32                    os_err = pr ? pr->os_error : 0;
    Mel_IO_Status          st = pr ? pipe_status_from_port(pr->status) : (MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    mel_port_future_release(k->port_future);

    Pipe_Op*         op = k->op;
    const Mel_Alloc* alloc = op->alloc;
    pipe_op_resolve(op, bytes, os_err, st);
    mel_dealloc(alloc, k);
}

static Mel_Future* pipe_submit(Mel_Stream* s, Pipe_State* f, bool is_read, void* buf, usize len, Mel_Executor* deliver)
{
    if (!f->port || !mel_port_available(f->port))
    {
        Pipe_Op* op = pipe_op_new(f->alloc);
        if (!op)
            return NULL;
        return pipe_op_resolve(op, 0, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    }

    Pipe_Op* op = pipe_op_new(f->alloc);
    if (!op)
        return NULL;

    assert(mel_reactor_is_owner(mel_port_reactor(f->port)));

    Mel_Port_Op port_op = MEL_PORT_OP_NULL;
    Mel_Future* pf;
    if (is_read)
        pf = mel_port_read_opt(f->port, (Mel_Port_Read_Opt){ .fd = f->fd, .buffer = buf, .len = len, .offset = MEL_PORT_NO_OFFSET, .deliver = deliver, .out_op = &port_op });
    else
        pf = mel_port_write_opt(f->port, (Mel_Port_Write_Opt){ .fd = f->fd, .buffer = (const void*)buf, .len = len, .offset = MEL_PORT_NO_OFFSET, .deliver = deliver, .out_op = &port_op });

    if (!pf)
        return pipe_op_resolve(op, 0, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    Pipe_Cont* k = mel_alloc_type(f->alloc, Pipe_Cont);
    if (!k)
    {
        mel_port_cancel(f->port, port_op);
        mel_port_future_release(pf);
        return pipe_op_resolve(op, 0, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    }
    memset(k, 0, sizeof *k);
    k->port_future = pf;
    k->op = op;
    k->stream = s;
    mel_task_init(&k->task, pipe_cont_run);
    mel_future_then(pf, &k->task, deliver ? deliver : mel_port_executor(f->port));
    return &op->future;
}

static Mel_Future* pipe_read(Mel_Stream* s, void* user, Mel_Stream_Read_Opt opt)
{
    Pipe_State* f = (Pipe_State*)user;
    return pipe_submit(s, f, true, opt.buffer, opt.len, opt.deliver);
}

static Mel_Future* pipe_write(Mel_Stream* s, void* user, Mel_Stream_Write_Opt opt)
{
    Pipe_State* f = (Pipe_State*)user;
    return pipe_submit(s, f, false, (void*)opt.buffer, opt.len, opt.deliver);
}

static void pipe_close(Mel_Stream* s, void* user)
{
    Pipe_State* f = (Pipe_State*)user;
    if (f->fd >= 0)
        close(f->fd);
    if (f->port)
        mel_port_destroy(f->port);
    mel_dealloc(mel_stream_alloc(s), f);
}

const Mel_Stream_Iface MEL_PROCESS_PIPE_IFACE = {
    .name = "process-pipe",
    .read = pipe_read,
    .write = pipe_write,
    .close = pipe_close,
};

static const Mel_Stream_Iface* pipe_iface(void) { return &MEL_PROCESS_PIPE_IFACE; }

Mel_Stream* mel_process__pipe_stream(i32 fd, bool readable, bool writable, Mel_Reactor* reactor, const Mel_Alloc* alloc)
{
    if (fd < 0)
        return NULL;
    if (!reactor)
    {
        mel_log_error("process", "pipe stream requires a reactor for async byte transfer");
        return NULL;
    }

    Pipe_State* f = mel_alloc_type(alloc, Pipe_State);
    if (!f)
        return NULL;
    memset(f, 0, sizeof *f);
    f->fd = fd;
    f->readable = readable;
    f->writable = writable;
    f->alloc = alloc;
    f->port = mel_port_create(.reactor = reactor, .alloc = alloc);

    bool async = f->port && mel_port_available(f->port);

    Mel_Stream* s = mel_stream_create(.iface = pipe_iface(),
                                      .user = f,
                                      .alloc = alloc,
                                      .reactor = reactor,
                                      .executor = mel_reactor_executor(reactor),
                                      .caps = { .readable = readable, .writable = writable, .async = async });
    if (!s)
    {
        if (f->port)
            mel_port_destroy(f->port);
        mel_dealloc(alloc, f);
        return NULL;
    }
    return s;
}

bool mel_process__pipe_fd(const Mel_Stream* s, i32* out_fd)
{
    const char* name = mel_stream_iface_name(s);
    if (!name || name != MEL_PROCESS_PIPE_IFACE.name)
        return false;
    Pipe_State* f = (Pipe_State*)mel_stream_user(s);
    if (!f || f->fd < 0)
        return false;
    if (out_fd)
        *out_fd = f->fd;
    return true;
}
