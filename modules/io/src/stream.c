#include "io_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <executor/executor.h>
#include <collection.list/list.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

Mel_Future_Status mel_io__future_status_from(Mel_IO_Status status)
{
    Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
    if (status & MEL_IO_PARTIAL)
        fs |= MEL_FUTURE_PARTIAL;
    if (status & MEL_IO_CANCELLED)
        fs |= MEL_FUTURE_CANCELLED;
    return fs;
}

Mel_IO_Op* mel_io__op_new(const Mel_Alloc* alloc)
{
    Mel_IO_Op* op = mel_alloc_type(alloc, Mel_IO_Op);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);
    op->alloc = alloc;
    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;
    return op;
}

Mel_Future* mel_io__op_resolve(Mel_IO_Op* op, usize bytes, i64 position, i32 os_error, Mel_IO_Status status)
{
    op->result.bytes_transferred = bytes;
    op->result.position = position;
    op->result.os_error = os_error;
    op->result.status = status;
    if (status & MEL_IO_CANCELLED)
        mel_future_cancel(&op->future);
    else
        mel_future_resolve(&op->future, &op->result, mel_io__future_status_from(status));
    return &op->future;
}

Mel_Stream* mel_stream_create_opt(Mel_Stream_Opt opt)
{
    if (!opt.iface)
    {
        mel_log_error("io", "stream create: iface is required");
        return NULL;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Stream*      s = mel_alloc_type(alloc, Mel_Stream);
    if (!s)
        return NULL;
    memset(s, 0, sizeof *s);

    s->iface = opt.iface;
    s->user = opt.user;
    s->caps = opt.caps;
    s->alloc = alloc;
    s->reactor = opt.reactor;
    s->executor = opt.executor;
    s->position = 0;
    return s;
}

void mel_stream_destroy(Mel_Stream* s)
{
    if (!s)
        return;
    if (s->iface && s->iface->close)
        s->iface->close(s, s->user);
    const Mel_Alloc* alloc = s->alloc;
    mel_dealloc(alloc, s);
}

Mel_Stream_Caps  mel_stream_caps(const Mel_Stream* s) { return s ? s->caps : (Mel_Stream_Caps){ 0 }; }
i64              mel_stream_position(const Mel_Stream* s) { return s ? s->position : 0; }
const Mel_Alloc* mel_stream_alloc(const Mel_Stream* s) { return s ? s->alloc : NULL; }
Mel_Reactor*     mel_stream_reactor(const Mel_Stream* s) { return s ? s->reactor : NULL; }
Mel_Executor*    mel_stream_executor(const Mel_Stream* s) { return s ? s->executor : NULL; }
void*            mel_stream_user(const Mel_Stream* s) { return s ? s->user : NULL; }
const char*      mel_stream_iface_name(const Mel_Stream* s) { return (s && s->iface) ? s->iface->name : NULL; }

static Mel_Future* fail_future(Mel_Stream* s, Mel_Executor* deliver, Mel_IO_Status status)
{
    Mel_IO_Op* op = mel_io__op_new(s->alloc);
    if (!op)
        return NULL;
    (void)deliver;
    return mel_io__op_resolve(op, 0, s->position, 0, status);
}

Mel_Future* mel_stream_read_opt(Mel_Stream* s, Mel_Stream_Read_Opt opt)
{
    if (!s)
        return NULL;
    if (!s->caps.readable)
    {
        mel_log_error("io", "read on non-readable stream '%s'", s->iface ? s->iface->name : "?");
        return fail_future(s, opt.deliver, MEL_IO_ERROR | MEL_IO_WRITE_ONLY);
    }
    if (!s->iface->read)
        return fail_future(s, opt.deliver, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    return s->iface->read(s, s->user, opt);
}

Mel_Future* mel_stream_write_opt(Mel_Stream* s, Mel_Stream_Write_Opt opt)
{
    if (!s)
        return NULL;
    if (!s->caps.writable)
    {
        mel_log_error("io", "write on non-writable stream '%s'", s->iface ? s->iface->name : "?");
        return fail_future(s, opt.deliver, MEL_IO_ERROR | MEL_IO_READ_ONLY);
    }
    if (!s->iface->write)
        return fail_future(s, opt.deliver, MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    return s->iface->write(s, s->user, opt);
}

Mel_Future* mel_stream_flush_ex(Mel_Stream* s, Mel_Executor* deliver)
{
    if (!s)
        return NULL;
    if (s->iface->flush)
        return s->iface->flush(s, s->user, deliver);
    Mel_IO_Op* op = mel_io__op_new(s->alloc);
    if (!op)
        return NULL;
    return mel_io__op_resolve(op, 0, s->position, 0, MEL_IO_OK);
}

const Mel_IO_Result* mel_stream_future_result(Mel_Future* f)
{
    if (!f)
        return NULL;
    Mel_IO_Op* op = mel_container_of(f, Mel_IO_Op, future);
    return &op->result;
}

void mel_stream_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Mel_IO_Op* op = mel_container_of(f, Mel_IO_Op, future);
    mel_dealloc(op->alloc, op);
}

Mel_IO_Status mel_stream_seek(Mel_Stream* s, i64 offset, i32 whence, i64* out_pos)
{
    if (!s)
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    if (!s->caps.seekable || !s->iface->seek)
        return MEL_IO_ERROR | MEL_IO_NOT_SEEKABLE;
    i64           pos = s->position;
    Mel_IO_Status st = s->iface->seek(s, s->user, offset, whence, &pos);
    if (!mel_io_status_failed(st))
        s->position = pos;
    if (out_pos)
        *out_pos = s->position;
    return st;
}

i64 mel_stream_tell(const Mel_Stream* s) { return s ? s->position : 0; }

Mel_IO_Status mel_stream_size(Mel_Stream* s, i64* out_size)
{
    if (!s)
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    if (!s->caps.sized || !s->iface->size)
        return MEL_IO_ERROR | MEL_IO_UNAVAILABLE;
    i64 sz = 0;
    if (!s->iface->size(s, s->user, &sz))
        return MEL_IO_ERROR;
    if (out_size)
        *out_size = sz;
    return MEL_IO_OK;
}

static Mel_IO_Result drain_sync(Mel_Future* f, Mel_Stream* s)
{
    Mel_IO_Result r = { 0 };
    if (!f)
    {
        r.status = MEL_IO_ERROR | MEL_IO_UNAVAILABLE;
        return r;
    }
    if (!mel_future_resolved(&((Mel_IO_Op*)mel_container_of(f, Mel_IO_Op, future))->future))
    {
        mel_stream_future_release(f);
        r.status = MEL_IO_ERROR | MEL_IO_UNAVAILABLE;
        mel_log_error("io", "sync op on '%s' did not resolve immediately; use async ops for this backend", s && s->iface ? s->iface->name : "?");
        return r;
    }
    const Mel_IO_Result* res = mel_stream_future_result(f);
    r = *res;
    mel_stream_future_release(f);
    return r;
}

Mel_IO_Result mel_stream_read_sync(Mel_Stream* s, void* buffer, usize len, i64 offset)
{
    Mel_Future* f = mel_stream_read_opt(s, (Mel_Stream_Read_Opt){ .buffer = buffer, .len = len, .offset = offset, .deliver = mel_executor_inline() });
    return drain_sync(f, s);
}

Mel_IO_Result mel_stream_write_sync(Mel_Stream* s, const void* buffer, usize len, i64 offset)
{
    Mel_Future* f = mel_stream_write_opt(s, (Mel_Stream_Write_Opt){ .buffer = buffer, .len = len, .offset = offset, .deliver = mel_executor_inline() });
    return drain_sync(f, s);
}

Mel_IO_Status mel_stream_read_exact(Mel_Stream* s, void* buffer, usize len)
{
    u8*   p = (u8*)buffer;
    usize done = 0;
    while (done < len)
    {
        Mel_IO_Result r = mel_stream_read_sync(s, p + done, len - done, MEL_IO_NO_OFFSET);
        if (mel_io_status_failed(r.status))
            return r.status;
        if (r.bytes_transferred == 0)
            return MEL_IO_ERROR | MEL_IO_EOF | (done > 0 ? MEL_IO_PARTIAL : 0u);
        done += r.bytes_transferred;
    }
    return MEL_IO_OK;
}

Mel_IO_Status mel_stream_write_all(Mel_Stream* s, const void* buffer, usize len)
{
    const u8* p = (const u8*)buffer;
    usize     done = 0;
    while (done < len)
    {
        Mel_IO_Result r = mel_stream_write_sync(s, p + done, len - done, MEL_IO_NO_OFFSET);
        if (mel_io_status_failed(r.status))
            return r.status;
        if (r.bytes_transferred == 0)
            return MEL_IO_ERROR | MEL_IO_NO_SPACE | (done > 0 ? MEL_IO_PARTIAL : 0u);
        done += r.bytes_transferred;
    }
    return MEL_IO_OK;
}

Mel_IO_Status mel_stream_read_u8(Mel_Stream* s, u8* out) { return mel_stream_read_exact(s, out, 1); }
Mel_IO_Status mel_stream_write_u8(Mel_Stream* s, u8 v) { return mel_stream_write_all(s, &v, 1); }

#define MEL_IO__READ_LE(bits)                                               \
    Mel_IO_Status mel_stream_read_u##bits##_le(Mel_Stream* s, u##bits* out) \
    {                                                                       \
        u8            b[bits / 8];                                          \
        Mel_IO_Status st = mel_stream_read_exact(s, b, sizeof b);           \
        if (mel_io_status_failed(st))                                       \
            return st;                                                      \
        u##bits v = 0;                                                      \
        for (usize i = 0; i < sizeof b; i++)                                \
            v |= (u##bits)b[i] << (8u * i);                                 \
        *out = v;                                                           \
        return st;                                                          \
    }

#define MEL_IO__READ_BE(bits)                                               \
    Mel_IO_Status mel_stream_read_u##bits##_be(Mel_Stream* s, u##bits* out) \
    {                                                                       \
        u8            b[bits / 8];                                          \
        Mel_IO_Status st = mel_stream_read_exact(s, b, sizeof b);           \
        if (mel_io_status_failed(st))                                       \
            return st;                                                      \
        u##bits v = 0;                                                      \
        for (usize i = 0; i < sizeof b; i++)                                \
            v = (u##bits)(v << 8) | b[i];                                   \
        *out = v;                                                           \
        return st;                                                          \
    }

#define MEL_IO__WRITE_LE(bits)                                            \
    Mel_IO_Status mel_stream_write_u##bits##_le(Mel_Stream* s, u##bits v) \
    {                                                                     \
        u8 b[bits / 8];                                                   \
        for (usize i = 0; i < sizeof b; i++)                              \
            b[i] = (u8)(v >> (8u * i));                                   \
        return mel_stream_write_all(s, b, sizeof b);                      \
    }

#define MEL_IO__WRITE_BE(bits)                                            \
    Mel_IO_Status mel_stream_write_u##bits##_be(Mel_Stream* s, u##bits v) \
    {                                                                     \
        u8 b[bits / 8];                                                   \
        for (usize i = 0; i < sizeof b; i++)                              \
            b[i] = (u8)(v >> (8u * (sizeof b - 1 - i)));                  \
        return mel_stream_write_all(s, b, sizeof b);                      \
    }

MEL_IO__READ_LE(16)
MEL_IO__READ_LE(32)
MEL_IO__READ_LE(64)
MEL_IO__READ_BE(16)
MEL_IO__READ_BE(32)
MEL_IO__READ_BE(64)
MEL_IO__WRITE_LE(16)
MEL_IO__WRITE_LE(32)
MEL_IO__WRITE_LE(64)
MEL_IO__WRITE_BE(16)
MEL_IO__WRITE_BE(32)
MEL_IO__WRITE_BE(64)

void mel_stream__set_position(Mel_Stream* s, i64 position) { s->position = position; }
i64  mel_stream__position(const Mel_Stream* s) { return s->position; }

bool mel_stream_native_fd(const Mel_Stream* s, i32* out_fd)
{
    if (!s || s->iface != mel_io__file_iface())
        return false;
    return mel_io__file_native_fd(s, out_fd);
}

bool mel_stream_native_file_handle(const Mel_Stream* s, void** out_handle)
{
    if (!s || s->iface != mel_io__file_iface())
        return false;
    return mel_io__file_native_handle(s, out_handle);
}

bool mel_stream_native_memory(const Mel_Stream* s, void** out_base, usize* out_len)
{
    if (!s)
        return false;
    if (s->iface != mel_io__memory_rw_iface() && s->iface != mel_io__memory_const_iface() && s->iface != mel_io__memory_growable_iface())
        return false;
    return mel_io__memory_native(s, out_base, out_len);
}
