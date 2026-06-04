#include "io_internal.h"

#include <io/file.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <executor/executor.h>
#include <reactor/reactor.h>
#include <collection/list.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    Mel_Future       future;
    Mel_IO_Blob      blob;
    const Mel_Alloc* alloc;
} Load_Op;

typedef struct
{
    Mel_Task    task;
    Load_Op*    op;
    Mel_Stream* stream;
    u8*         data;
    usize       len;
    usize       got;
    Mel_Future* pending;
} Load_Drive;

static void load_blob_free(void* value, const Mel_Alloc* alloc)
{
    Mel_IO_Blob* b = (Mel_IO_Blob*)value;
    if (b && b->data)
        mel_dealloc(alloc, b->data);
}

static Load_Op* load_op_new(const Mel_Alloc* alloc)
{
    Load_Op* op = mel_alloc_type(alloc, Load_Op);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);
    op->alloc = alloc;
    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->blob;
    return op;
}

const Mel_IO_Blob* mel_io_load_future_result(Mel_Future* f)
{
    if (!f)
        return NULL;
    Load_Op* op = mel_container_of(f, Load_Op, future);
    return &op->blob;
}

void mel_io_load_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Load_Op* op = mel_container_of(f, Load_Op, future);
    if (op->blob.data)
        mel_dealloc(op->alloc, op->blob.data);
    mel_dealloc(op->alloc, op);
}

static Mel_Future* load_resolve(Load_Op* op, u8* data, usize len, Mel_IO_Status status)
{
    op->blob.data = data;
    op->blob.len = len;
    op->blob.status = status;
    if (status & MEL_IO_CANCELLED)
        mel_future_cancel(&op->future);
    else
        mel_future_resolve(&op->future, &op->blob, mel_io__future_status_from(status));
    return &op->future;
}

static void load_step(Load_Drive* d);

static void load_cont(Mel_Task* self)
{
    Load_Drive*          d = mel_container_of(self, Load_Drive, task);
    const Mel_IO_Result* r = mel_stream_future_result(d->pending);
    usize                n = r ? r->bytes_transferred : 0;
    Mel_IO_Status        st = r ? r->status : (MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    mel_stream_future_release(d->pending);
    d->pending = NULL;

    if (mel_io_status_failed(st) || n == 0)
    {
        Load_Op*         op = d->op;
        Mel_Stream*      s = d->stream;
        u8*              data = d->data;
        usize            got = d->got;
        const Mel_Alloc* alloc = s->alloc;
        mel_dealloc(alloc, d);
        mel_stream_destroy(s);
        if (mel_io_status_failed(st))
        {
            mel_dealloc(op->alloc, data);
            load_resolve(op, NULL, 0, st);
        }
        else
        {
            load_resolve(op, data, got, MEL_IO_OK | (got < op->blob.len ? MEL_IO_TRUNCATED : 0u));
        }
        return;
    }

    d->got += n;
    if (d->got >= d->len)
    {
        Load_Op*         op = d->op;
        Mel_Stream*      s = d->stream;
        u8*              data = d->data;
        usize            got = d->got;
        const Mel_Alloc* alloc = s->alloc;
        mel_dealloc(alloc, d);
        mel_stream_destroy(s);
        load_resolve(op, data, got, MEL_IO_OK);
        return;
    }
    load_step(d);
}

static void load_step(Load_Drive* d)
{
    Mel_Future* f = mel_stream_read(d->stream, .buffer = d->data + d->got, .len = d->len - d->got);
    if (!f)
    {
        Load_Op*         op = d->op;
        Mel_Stream*      s = d->stream;
        u8*              data = d->data;
        const Mel_Alloc* alloc = s->alloc;
        mel_dealloc(alloc, d);
        mel_stream_destroy(s);
        mel_dealloc(op->alloc, data);
        load_resolve(op, NULL, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
        return;
    }
    d->pending = f;
    mel_task_init(&d->task, load_cont);
    mel_future_then(f, &d->task, mel_stream_executor(d->stream) ? mel_stream_executor(d->stream) : mel_executor_inline());
}

static bool deliver_ok(Mel_Executor* deliver, Mel_Reactor* reactor, const char* op)
{
    if (!deliver)
        return true;
    Mel_Executor* expected = reactor ? mel_reactor_executor(reactor) : mel_executor_inline();
    if (deliver == expected)
        return true;
    mel_log_error("io", "%s: deliver executor must be %s; pass that or leave NULL", op, reactor ? "the reactor's executor" : "the inline executor");
    return false;
}

Mel_Future* mel_io_load_file_opt(Mel_IO_Load_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Load_Op*         op = load_op_new(alloc);
    if (!op)
        return NULL;
    op->future.free_value = load_blob_free;

    if (!deliver_ok(opt.deliver, opt.reactor, "load_file"))
        return load_resolve(op, NULL, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    Mel_IO_File_Open_Result o = mel_io_file_open(.path = opt.path, .flags = MEL_IO_FILE_READ, .reactor = opt.reactor, .alloc = alloc);
    if (mel_io_status_failed(o.status))
        return load_resolve(op, NULL, 0, o.status);

    i64 size = 0;
    if (mel_io_status_failed(mel_stream_size(o.value, &size)) || size < 0)
    {
        mel_stream_destroy(o.value);
        return load_resolve(op, NULL, 0, MEL_IO_ERROR | MEL_IO_NOT_SEEKABLE);
    }

    if (size == 0)
    {
        mel_stream_destroy(o.value);
        return load_resolve(op, NULL, 0, MEL_IO_OK);
    }

    u8* data = mel_alloc(alloc, (usize)size);
    if (!data)
    {
        mel_stream_destroy(o.value);
        return load_resolve(op, NULL, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    }

    if (!opt.reactor)
    {
        Mel_IO_Status st = mel_stream_read_exact(o.value, data, (usize)size);
        mel_stream_destroy(o.value);
        if (mel_io_status_failed(st))
        {
            mel_dealloc(alloc, data);
            return load_resolve(op, NULL, 0, st);
        }
        return load_resolve(op, data, (usize)size, MEL_IO_OK);
    }

    Load_Drive* d = mel_alloc_type(alloc, Load_Drive);
    if (!d)
    {
        mel_dealloc(alloc, data);
        mel_stream_destroy(o.value);
        return load_resolve(op, NULL, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    }
    memset(d, 0, sizeof *d);
    op->blob.len = (usize)size;
    d->op = op;
    d->stream = o.value;
    d->data = data;
    d->len = (usize)size;
    d->got = 0;
    load_step(d);
    return &op->future;
}

typedef struct
{
    Mel_Task    task;
    Mel_IO_Op*  op;
    Mel_Stream* stream;
    const u8*   data;
    usize       len;
    usize       sent;
    Mel_Future* pending;
} Save_Drive;

const Mel_IO_Result* mel_io_save_future_result(Mel_Future* f) { return mel_stream_future_result(f); }
void                 mel_io_save_future_release(Mel_Future* f) { mel_stream_future_release(f); }

static void save_step(Save_Drive* d);

static void save_finish(Save_Drive* d, usize sent, Mel_IO_Status st)
{
    Mel_IO_Op*       op = d->op;
    Mel_Stream*      s = d->stream;
    const Mel_Alloc* alloc = s->alloc;
    mel_dealloc(alloc, d);
    mel_stream_destroy(s);
    mel_io__op_resolve(op, sent, (i64)sent, 0, st);
}

static void save_cont(Mel_Task* self)
{
    Save_Drive*          d = mel_container_of(self, Save_Drive, task);
    const Mel_IO_Result* r = mel_stream_future_result(d->pending);
    usize                n = r ? r->bytes_transferred : 0;
    Mel_IO_Status        st = r ? r->status : (MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    mel_stream_future_release(d->pending);
    d->pending = NULL;

    if (mel_io_status_failed(st))
    {
        save_finish(d, d->sent, st);
        return;
    }
    d->sent += n;
    if (d->sent >= d->len || n == 0)
    {
        save_finish(d, d->sent, d->sent >= d->len ? MEL_IO_OK : (MEL_IO_ERROR | MEL_IO_NO_SPACE | MEL_IO_PARTIAL));
        return;
    }
    save_step(d);
}

static void save_step(Save_Drive* d)
{
    Mel_Future* f = mel_stream_write(d->stream, .buffer = d->data + d->sent, .len = d->len - d->sent);
    if (!f)
    {
        save_finish(d, d->sent, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
        return;
    }
    d->pending = f;
    mel_task_init(&d->task, save_cont);
    mel_future_then(f, &d->task, mel_stream_executor(d->stream) ? mel_stream_executor(d->stream) : mel_executor_inline());
}

Mel_Future* mel_io_save_file_opt(Mel_IO_Save_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_IO_Op*       op = mel_io__op_new(alloc);
    if (!op)
        return NULL;

    if (!deliver_ok(opt.deliver, opt.reactor, "save_file"))
        return mel_io__op_resolve(op, 0, 0, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);

    u32 flags = opt.flags;
    if ((flags & (MEL_IO_FILE_WRITE | MEL_IO_FILE_APPEND)) == 0)
        flags |= MEL_IO_FILE_WRITE | MEL_IO_FILE_CREATE | MEL_IO_FILE_TRUNCATE;
    if (flags & MEL_IO_FILE_APPEND)
        flags |= MEL_IO_FILE_WRITE | MEL_IO_FILE_CREATE;

    Mel_IO_File_Open_Result o = mel_io_file_open_opt((Mel_IO_File_Open_Opt){ .path = opt.path, .flags = flags, .mode = opt.mode, .reactor = opt.reactor, .alloc = alloc });
    if (mel_io_status_failed(o.status))
        return mel_io__op_resolve(op, 0, 0, 0, o.status);

    if (opt.len == 0)
    {
        mel_stream_destroy(o.value);
        return mel_io__op_resolve(op, 0, 0, 0, MEL_IO_OK);
    }

    if (!opt.reactor)
    {
        Mel_IO_Status st = mel_stream_write_all(o.value, opt.data, opt.len);
        mel_stream_destroy(o.value);
        return mel_io__op_resolve(op, mel_io_status_failed(st) ? 0 : opt.len, (i64)opt.len, 0, st);
    }

    Save_Drive* d = mel_alloc_type(alloc, Save_Drive);
    if (!d)
    {
        mel_stream_destroy(o.value);
        return mel_io__op_resolve(op, 0, 0, 0, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    }
    memset(d, 0, sizeof *d);
    d->op = op;
    d->stream = o.value;
    d->data = (const u8*)opt.data;
    d->len = opt.len;
    save_step(d);
    return &op->future;
}
