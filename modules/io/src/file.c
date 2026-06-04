#include "io_internal.h"

#include <io/file.h>
#include <io/backend.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <executor/executor.h>
#include <reactor/reactor.h>
#include <port/port.h>
#include <collection.list/list.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

typedef struct
{
    Mel_IO_File_Native native;
    Mel_Port*          port;
    bool               owns_port;
    bool               readable;
    bool               writable;
} File_State;

typedef struct
{
    Mel_Task    task;
    Mel_Future* port_future;
    Mel_IO_Op*  op;
    Mel_Stream* stream;
    i64         target_pos;
    bool        advance;
} File_Cont;

static Mel_IO_Status status_from_port(Mel_Port_Status ps)
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

static void file_cont_run(Mel_Task* self)
{
    File_Cont*             k = mel_container_of(self, File_Cont, task);
    const Mel_Port_Result* pr = mel_port_future_result(k->port_future);
    usize                  bytes = pr ? pr->bytes_transferred : 0;
    i32                    os_err = pr ? pr->os_error : 0;
    Mel_IO_Status          st = pr ? status_from_port(pr->status) : (MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    i64 new_pos = k->target_pos + (i64)bytes;
    if (k->advance && !mel_io_status_failed(st))
        mel_stream__set_position(k->stream, new_pos);

    mel_port_future_release(k->port_future);

    Mel_IO_Op*       op = k->op;
    const Mel_Alloc* alloc = op->alloc;
    mel_io__op_resolve(op, bytes, k->advance ? mel_stream__position(k->stream) : new_pos, os_err, st);
    mel_dealloc(alloc, k);
}

static Mel_Future* file_submit(Mel_Stream* s, File_State* f, bool is_read, void* buf, usize len, i64 offset, Mel_Executor* deliver)
{
    Mel_IO_Op* op = mel_io__op_new(s->alloc);
    if (!op)
        return NULL;

    bool advance = (offset == MEL_IO_NO_OFFSET);
    i64  pos = advance ? mel_stream__position(s) : offset;

    if (deliver == mel_executor_inline() || !f->port || !mel_port_available(f->port))
    {
        Mel_IO_Result r;
        Mel_IO_Status st = mel_io__backend_pio(f->native, is_read, buf, len, pos, &r);
        i64           new_pos = pos + (i64)r.bytes_transferred;
        if (advance && !mel_io_status_failed(st))
            mel_stream__set_position(s, new_pos);
        return mel_io__op_resolve(op, r.bytes_transferred, advance ? mel_stream__position(s) : new_pos, r.os_error, st);
    }

    assert(mel_reactor_is_owner(mel_port_reactor(f->port)));

    i64         port_off = f->native.seekable ? pos : MEL_PORT_NO_OFFSET;
    Mel_Port_Op port_op = MEL_PORT_OP_NULL;
    Mel_Future* pf;
    if (is_read)
        pf = mel_port_read_opt(f->port, (Mel_Port_Read_Opt){ .fd = f->native.fd, .buffer = buf, .len = len, .offset = port_off, .deliver = deliver, .out_op = &port_op });
    else
        pf = mel_port_write_opt(f->port, (Mel_Port_Write_Opt){ .fd = f->native.fd, .buffer = (const void*)buf, .len = len, .offset = port_off, .deliver = deliver, .out_op = &port_op });

    if (!pf)
        return mel_io__op_resolve(op, 0, pos, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    File_Cont* k = mel_alloc_type(s->alloc, File_Cont);
    if (!k)
    {
        mel_port_cancel(f->port, port_op);
        mel_port_future_release(pf);
        return mel_io__op_resolve(op, 0, pos, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    }
    memset(k, 0, sizeof *k);
    k->port_future = pf;
    k->op = op;
    k->stream = s;
    k->target_pos = pos;
    k->advance = advance;
    mel_task_init(&k->task, file_cont_run);
    mel_future_then(pf, &k->task, deliver ? deliver : mel_port_executor(f->port));
    return &op->future;
}

static Mel_Future* file_read(Mel_Stream* s, void* user, Mel_Stream_Read_Opt opt)
{
    File_State* f = (File_State*)user;
    return file_submit(s, f, true, opt.buffer, opt.len, opt.offset, opt.deliver);
}

static Mel_Future* file_write(Mel_Stream* s, void* user, Mel_Stream_Write_Opt opt)
{
    File_State* f = (File_State*)user;
    return file_submit(s, f, false, (void*)opt.buffer, opt.len, opt.offset, opt.deliver);
}

static Mel_Future* file_flush(Mel_Stream* s, void* user, Mel_Executor* deliver)
{
    File_State* f = (File_State*)user;
    (void)deliver;
    Mel_IO_Op* op = mel_io__op_new(s->alloc);
    if (!op)
        return NULL;
    Mel_IO_Status st = mel_io__backend_flush(f->native);
    return mel_io__op_resolve(op, 0, mel_stream__position(s), 0, st);
}

static Mel_IO_Status file_seek(Mel_Stream* s, void* user, i64 offset, i32 whence, i64* out_pos)
{
    File_State* f = (File_State*)user;
    (void)s;
    return mel_io__backend_seek(f->native, offset, whence, out_pos);
}

static bool file_size(Mel_Stream* s, void* user, i64* out_size)
{
    File_State* f = (File_State*)user;
    (void)s;
    if (!f->writable && f->native.seekable)
    {
        *out_size = f->native.initial_size;
        return true;
    }
    return mel_io__backend_size(f->native, out_size);
}

static void file_close(Mel_Stream* s, void* user)
{
    File_State* f = (File_State*)user;
    mel_io__backend_close(f->native);
    if (f->owns_port && f->port)
        mel_port_destroy(f->port);
    mel_dealloc(s->alloc, f);
}

static const Mel_Stream_Iface FILE_IFACE = {
    .name = "file",
    .read = file_read,
    .write = file_write,
    .flush = file_flush,
    .seek = file_seek,
    .size = file_size,
    .close = file_close,
};

const Mel_Stream_Iface* mel_io__file_iface(void) { return &FILE_IFACE; }

bool mel_io__file_native_fd(const Mel_Stream* s, i32* out_fd)
{
    File_State* f = (File_State*)mel_stream_user(s);
    if (!f || f->native.fd < 0)
        return false;
    if (out_fd)
        *out_fd = f->native.fd;
    return true;
}

bool mel_io__file_native_handle(const Mel_Stream* s, void** out_handle)
{
    File_State* f = (File_State*)mel_stream_user(s);
    if (!f || !f->native.handle)
        return false;
    if (out_handle)
        *out_handle = f->native.handle;
    return true;
}

bool mel_io_file_available(void) { return mel_io__backend_available(); }

Mel_IO_File_Open_Result mel_io_file_open_opt(Mel_IO_File_Open_Opt opt)
{
    Mel_IO_File_Open_Result out = { 0 };

    if (!opt.path)
    {
        mel_log_error("io", "file_open: path is required");
        out.status = MEL_IO_ERROR | MEL_IO_NOT_FOUND;
        return out;
    }
    if ((opt.flags & (MEL_IO_FILE_READ | MEL_IO_FILE_WRITE)) == 0)
    {
        mel_log_error("io", "file_open: neither read nor write requested for '%s'", opt.path);
        out.status = MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
        return out;
    }
    if (!mel_io__backend_available())
    {
        mel_log_warn("io", "file_open: no file backend on this platform");
        out.status = MEL_IO_ERROR | MEL_IO_UNAVAILABLE;
        return out;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_IO_File_Native native = mel_io__backend_open(opt.path, opt.flags, opt.mode);
    if (mel_io_status_failed(native.status))
    {
        out.status = native.status;
        return out;
    }

    File_State* f = mel_alloc_type(alloc, File_State);
    if (!f)
    {
        mel_io__backend_close(native);
        out.status = MEL_IO_ERROR | MEL_IO_UNAVAILABLE;
        return out;
    }
    memset(f, 0, sizeof *f);
    f->native = native;
    f->readable = (opt.flags & MEL_IO_FILE_READ) != 0;
    f->writable = (opt.flags & MEL_IO_FILE_WRITE) != 0;

    if (opt.reactor)
    {
        f->port = mel_port_create(.reactor = opt.reactor, .alloc = alloc);
        f->owns_port = true;
    }

    Mel_Stream* s = mel_stream_create(.iface = &FILE_IFACE,
                                      .user = f,
                                      .alloc = alloc,
                                      .reactor = opt.reactor,
                                      .executor = opt.reactor ? mel_reactor_executor(opt.reactor) : NULL,
                                      .caps = { .readable = f->readable, .writable = f->writable, .seekable = native.seekable, .sized = native.seekable, .async = (opt.reactor && f->port && mel_port_available(f->port)), .size_bytes = native.initial_size });
    if (!s)
    {
        if (f->owns_port && f->port)
            mel_port_destroy(f->port);
        mel_io__backend_close(native);
        mel_dealloc(alloc, f);
        out.status = MEL_IO_ERROR | MEL_IO_UNAVAILABLE;
        return out;
    }

    if (opt.flags & MEL_IO_FILE_APPEND)
    {
        i64 end = 0;
        if (mel_io__backend_size(native, &end))
            mel_stream__set_position(s, end);
    }

    out.value = s;
    out.status = native.status;
    return out;
}
