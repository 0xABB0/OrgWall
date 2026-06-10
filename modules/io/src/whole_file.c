#include "io_internal.h"

#include <io/file.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <executor/executor.h>
#include <collection/list.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    Mel_Future       future;
    Mel_IO_Blob      blob;
    const Mel_Alloc* alloc;
} Load_Op;

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

Mel_Future* mel_io_load_file_opt(Mel_IO_Load_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Load_Op*         op = load_op_new(alloc);
    if (!op)
        return NULL;
    op->future.free_value = load_blob_free;

    Mel_IO_File_Open_Result o = mel_io_file_open(.path = opt.path, .flags = MEL_IO_FILE_READ, .vat = opt.vat, .alloc = alloc);
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

    Mel_IO_Status st = mel_stream_read_exact(o.value, data, (usize)size);
    mel_stream_destroy(o.value);
    if (mel_io_status_failed(st))
    {
        mel_dealloc(alloc, data);
        return load_resolve(op, NULL, 0, st);
    }
    return load_resolve(op, data, (usize)size, MEL_IO_OK);
}

const Mel_IO_Result* mel_io_save_future_result(Mel_Future* f) { return mel_stream_future_result(f); }
void                 mel_io_save_future_release(Mel_Future* f) { mel_stream_future_release(f); }

Mel_Future* mel_io_save_file_opt(Mel_IO_Save_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_IO_Op*       op = mel_io__op_new(alloc);
    if (!op)
        return NULL;

    u32 flags = opt.flags;
    if ((flags & (MEL_IO_FILE_WRITE | MEL_IO_FILE_APPEND)) == 0)
        flags |= MEL_IO_FILE_WRITE | MEL_IO_FILE_CREATE | MEL_IO_FILE_TRUNCATE;
    if (flags & MEL_IO_FILE_APPEND)
        flags |= MEL_IO_FILE_WRITE | MEL_IO_FILE_CREATE;

    Mel_IO_File_Open_Result o = mel_io_file_open_opt((Mel_IO_File_Open_Opt){ .path = opt.path, .flags = flags, .mode = opt.mode, .vat = opt.vat, .alloc = alloc });
    if (mel_io_status_failed(o.status))
        return mel_io__op_resolve(op, 0, 0, 0, o.status);

    if (opt.len == 0)
    {
        mel_stream_destroy(o.value);
        return mel_io__op_resolve(op, 0, 0, 0, MEL_IO_OK);
    }

    Mel_IO_Status st = mel_stream_write_all(o.value, opt.data, opt.len);
    mel_stream_destroy(o.value);
    return mel_io__op_resolve(op, mel_io_status_failed(st) ? 0 : opt.len, (i64)opt.len, 0, st);
}
