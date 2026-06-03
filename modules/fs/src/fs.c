#include "fs_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <collection.array/array.h>
#include <collection.mpsc/mpsc.h>
#include <collection.list/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <reactor/reactor.h>
#include <thread/thread.h>
#include <thread/sem.h>
#include <log/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>

Mel_Fs_Status mel_fs__status_from_errno(i32 err)
{
    switch (err)
    {
    case 0:
        return MEL_FS_OK;
    case ENOENT:
        return MEL_FS_ERROR | MEL_FS_NOT_FOUND;
    case EEXIST:
        return MEL_FS_ERROR | MEL_FS_EXISTS;
    case EACCES:
    case EPERM:
        return MEL_FS_ERROR | MEL_FS_PERMISSION;
    case ENOTDIR:
        return MEL_FS_ERROR | MEL_FS_NOT_A_DIRECTORY;
    case EISDIR:
        return MEL_FS_ERROR | MEL_FS_IS_A_DIRECTORY;
    case ENOTEMPTY:
        return MEL_FS_ERROR | MEL_FS_NOT_EMPTY;
    case ENOSPC:
        return MEL_FS_ERROR | MEL_FS_NO_SPACE;
    case ELOOP:
        return MEL_FS_ERROR | MEL_FS_LOOP;
    case ENAMETOOLONG:
        return MEL_FS_ERROR | MEL_FS_NAME_TOO_LONG;
    case EXDEV:
        return MEL_FS_ERROR | MEL_FS_CROSS_DEVICE;
    case EROFS:
        return MEL_FS_ERROR | MEL_FS_READ_ONLY;
    default:
        return MEL_FS_ERROR;
    }
}

static void result_set_status(Mel_Fs_Op_Record* op, Mel_Fs_Status status, i32 os_error)
{
    switch (op->kind)
    {
    case MEL_FS_JOB_STAT:
        op->result.stat.status = status;
        op->result.stat.os_error = os_error;
        break;
    case MEL_FS_JOB_EXISTS:
        op->result.boolean.status = status;
        op->result.boolean.os_error = os_error;
        break;
    case MEL_FS_JOB_MKDIR:
    case MEL_FS_JOB_REMOVE:
    case MEL_FS_JOB_RENAME:
    case MEL_FS_JOB_COPY:
    case MEL_FS_JOB_WRITE_FILE:
        op->result.voided.status = status;
        op->result.voided.os_error = os_error;
        break;
    case MEL_FS_JOB_READ_FILE:
        op->result.bytes.status = status;
        op->result.bytes.os_error = os_error;
        break;
    case MEL_FS_JOB_ENUMERATE:
    case MEL_FS_JOB_GLOB:
        op->result.dir.status = status;
        op->result.dir.os_error = os_error;
        break;
    default:
        break;
    }
}

static Mel_Fs_Status result_status(const Mel_Fs_Op_Record* op)
{
    switch (op->kind)
    {
    case MEL_FS_JOB_STAT:
        return op->result.stat.status;
    case MEL_FS_JOB_EXISTS:
        return op->result.boolean.status;
    case MEL_FS_JOB_READ_FILE:
        return op->result.bytes.status;
    case MEL_FS_JOB_ENUMERATE:
    case MEL_FS_JOB_GLOB:
        return op->result.dir.status;
    default:
        return op->result.voided.status;
    }
}

static void op_free_owned(Mel_Fs_Op_Record* op)
{
    const Mel_Alloc* a = op->alloc;
    if (op->path_a.data)
        mel_dealloc(a, op->path_a.data);
    if (op->path_b.data)
        mel_dealloc(a, op->path_b.data);
    if (op->glob_pattern.data)
        mel_dealloc(a, op->glob_pattern.data);
    op->path_a = STR8_EMPTY;
    op->path_b = STR8_EMPTY;
    op->glob_pattern = STR8_EMPTY;
}

void mel_fs__op_settle(Mel_Fs_Op_Record* op, Mel_Fs_Status status, i32 os_error)
{
    assert(mel_reactor_is_owner(op->fs->reactor));
    if (op->settled)
        return;
    op->settled = true;

    result_set_status(op, status, os_error);
    op_free_owned(op);

    if (!op->detached)
    {
        op->detached = true;
        mel_slotmap_remove(&op->fs->ops, op->self);
    }

    if (status & MEL_FS_CANCELLED)
    {
        mel_future_cancel(&op->future);
    }
    else
    {
        Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
        if (status & MEL_FS_PARTIAL)
            fs |= MEL_FUTURE_PARTIAL;
        mel_future_resolve(&op->future, &op->result, fs);
    }
}

void mel_fs__job_run(Mel_Fs_Op_Record* op)
{
    switch (op->kind)
    {
    case MEL_FS_JOB_STAT:
        mel_fs__do_stat(op);
        break;
    case MEL_FS_JOB_EXISTS:
        mel_fs__do_exists(op);
        break;
    case MEL_FS_JOB_MKDIR:
        mel_fs__do_mkdir(op);
        break;
    case MEL_FS_JOB_REMOVE:
        mel_fs__do_remove(op);
        break;
    case MEL_FS_JOB_RENAME:
        mel_fs__do_rename(op);
        break;
    case MEL_FS_JOB_COPY:
        mel_fs__do_copy(op);
        break;
    case MEL_FS_JOB_READ_FILE:
        mel_fs__do_read_file(op);
        break;
    case MEL_FS_JOB_WRITE_FILE:
        mel_fs__do_write_file(op);
        break;
    case MEL_FS_JOB_ENUMERATE:
        mel_fs__do_enumerate(op);
        break;
    case MEL_FS_JOB_GLOB:
        mel_fs__do_glob(op);
        break;
    default:
        result_set_status(op, MEL_FS_ERROR | MEL_FS_UNAVAILABLE, 0);
        break;
    }
}

static void completion_post(void* user)
{
    Mel_Fs_Op_Record* op = (Mel_Fs_Op_Record*)user;
    if (op->cancel_requested)
    {
        mel_fs__op_settle(op, MEL_FS_ERROR | MEL_FS_CANCELLED, 0);
        return;
    }
    mel_fs__op_settle(op, result_status(op), 0);
}

static int worker_main(void* user)
{
    Mel_Fs* fs = (Mel_Fs*)user;
    for (;;)
    {
        mel_sem_wait(&fs->queue_items);
        if (atomic_load_explicit(&fs->running, memory_order_acquire) == 0)
            break;

        Mel_Mpsc_Node* node = mel_mpsc_pop(&fs->queue);
        if (!node)
            continue;

        Mel_Fs_Op_Record* op = mel_container_of(node, Mel_Fs_Op_Record, queue_node);
        if (!op->cancel_requested)
            mel_fs__job_run(op);
        mel_reactor_post(fs->reactor, completion_post, op);
    }
    return 0;
}

Mel_Fs* mel_fs_create_opt(Mel_Fs_Opt opt)
{
    if (!opt.reactor)
    {
        mel_log_error("fs", "create: reactor is required");
        return NULL;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Fs*          fs = mel_alloc_type(alloc, Mel_Fs);
    if (!fs)
        return NULL;
    memset(fs, 0, sizeof *fs);

    fs->reactor = opt.reactor;
    fs->executor = mel_reactor_executor(opt.reactor);
    fs->alloc = alloc;
    fs->backend_ready = mel_fs__backend_available();

    mel_slotmap_init(&fs->ops, alloc, .item_size = sizeof(Mel_Fs_Op_Record*), .initial_capacity = 16);

    fs->worker_count = opt.worker_count == 0 ? 1 : opt.worker_count;
    mel_mpsc_init(&fs->queue);
    if (!mel_sem_init(&fs->queue_items, 0))
    {
        mel_log_error("fs", "create: semaphore init failed");
        mel_slotmap_free(&fs->ops);
        mel_dealloc(alloc, fs);
        return NULL;
    }

    atomic_store_explicit(&fs->running, 1, memory_order_release);
    fs->workers = mel_alloc_array(alloc, Mel_Thread, fs->worker_count);
    if (!fs->workers)
    {
        mel_sem_destroy(&fs->queue_items);
        mel_slotmap_free(&fs->ops);
        mel_dealloc(alloc, fs);
        return NULL;
    }

    u32 spawned = 0;
    for (u32 i = 0; i < fs->worker_count; i++)
    {
        if (!mel_thread_spawn(&fs->workers[i], worker_main, fs, .name = "mel-fs"))
        {
            mel_log_error("fs", "create: failed to spawn worker %u/%u", i, fs->worker_count);
            break;
        }
        spawned++;
    }
    if (spawned == 0)
    {
        atomic_store_explicit(&fs->running, 0, memory_order_release);
        mel_sem_destroy(&fs->queue_items);
        mel_dealloc(alloc, fs->workers);
        mel_slotmap_free(&fs->ops);
        mel_dealloc(alloc, fs);
        return NULL;
    }
    fs->worker_count = spawned;

    return fs;
}

void mel_fs_destroy(Mel_Fs* fs)
{
    if (!fs)
        return;
    assert(mel_reactor_is_owner(fs->reactor));

    Mel_Array(Mel_Fs_Op_Record*) snap;
    mel_array_init(&snap, fs->alloc);
    Mel_Fs_Op_Record** data = (Mel_Fs_Op_Record**)mel_slotmap_data(&fs->ops);
    u32                n = mel_slotmap_count(&fs->ops);
    for (u32 i = 0; i < n; i++)
        mel_array_push(&snap, data[i]);
    for (usize i = 0; i < snap.count; i++)
        snap.items[i]->cancel_requested = true;

    atomic_store_explicit(&fs->running, 0, memory_order_release);
    for (u32 i = 0; i < fs->worker_count; i++)
        mel_sem_post(&fs->queue_items);
    for (u32 i = 0; i < fs->worker_count; i++)
        mel_thread_join(&fs->workers[i], NULL);

    for (usize i = 0; i < snap.count; i++)
        mel_fs__op_settle(snap.items[i], MEL_FS_ERROR | MEL_FS_CANCELLED, 0);
    mel_array_free(&snap);

    mel_sem_destroy(&fs->queue_items);
    mel_slotmap_free(&fs->ops);

    const Mel_Alloc* alloc = fs->alloc;
    mel_dealloc(alloc, fs->workers);
    mel_dealloc(alloc, fs);
}

bool          mel_fs_available(const Mel_Fs* fs) { return fs && fs->backend_ready; }
Mel_Reactor*  mel_fs_reactor(const Mel_Fs* fs) { return fs ? fs->reactor : NULL; }
Mel_Executor* mel_fs_executor(const Mel_Fs* fs) { return fs ? fs->executor : NULL; }
u32           mel_fs_pending(const Mel_Fs* fs) { return fs ? mel_slotmap_count((Mel_SlotMap*)&fs->ops) : 0; }

static Mel_Fs_Op_Record* op_begin(Mel_Fs* fs, u32 kind, Mel_Executor* deliver, Mel_Fs_Op* out_op)
{
    const Mel_Alloc*  alloc = fs->alloc;
    Mel_Fs_Op_Record* op = mel_alloc_type(alloc, Mel_Fs_Op_Record);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);

    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;

    op->fs = fs;
    op->alloc = alloc;
    op->kind = kind;
    op->deliver = deliver ? deliver : fs->executor;

    Mel_Fs_Op_Record* slot = op;
    op->self = mel_slotmap_insert(&fs->ops, &slot);
    if (out_op)
        *out_op = (Mel_Fs_Op){ .index = op->self.index, .generation = op->self.generation };
    return op;
}

static Mel_Future* op_dup_path(Mel_Fs_Op_Record* op, str8* dst, str8 src)
{
    *dst = str8_dup_alloc(src, op->alloc);
    if (src.len > 0 && dst->data == NULL)
    {
        mel_fs__op_settle(op, MEL_FS_ERROR, ENOMEM);
        return &op->future;
    }
    return NULL;
}

static Mel_Future* op_submit(Mel_Fs_Op_Record* op)
{
    op->submitted = true;
    mel_mpsc_push(&op->fs->queue, &op->queue_node);
    mel_sem_post(&op->fs->queue_items);
    return &op->future;
}

static Mel_Future* op_fail_unavailable(Mel_Fs* fs, u32 kind, Mel_Executor* deliver, Mel_Fs_Op* out_op)
{
    Mel_Fs_Op_Record* op = op_begin(fs, kind, deliver, out_op);
    if (!op)
        return NULL;
    mel_log_error("fs", "submit: no filesystem backend available on this platform");
    mel_fs__op_settle(op, MEL_FS_ERROR | MEL_FS_UNAVAILABLE, 0);
    return &op->future;
}

Mel_Future* mel_fs_stat_opt(Mel_Fs* fs, str8 path, Mel_Fs_Stat_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_STAT, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_STAT, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->follow_symlinks = opt.follow_symlinks;
    Mel_Future* early = op_dup_path(op, &op->path_a, path);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_exists_opt(Mel_Fs* fs, str8 path, Mel_Fs_Exists_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_EXISTS, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_EXISTS, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    Mel_Future* early = op_dup_path(op, &op->path_a, path);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_mkdir_opt(Mel_Fs* fs, str8 path, Mel_Fs_Mkdir_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_MKDIR, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_MKDIR, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->mode_bits = opt.mode_bits;
    op->parents = opt.parents;
    Mel_Future* early = op_dup_path(op, &op->path_a, path);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_remove_opt(Mel_Fs* fs, str8 path, Mel_Fs_Remove_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_REMOVE, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_REMOVE, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->recursive = opt.recursive;
    Mel_Future* early = op_dup_path(op, &op->path_a, path);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_rename_opt(Mel_Fs* fs, str8 from, str8 to, Mel_Fs_Rename_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_RENAME, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_RENAME, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->overwrite = opt.overwrite;
    Mel_Future* early = op_dup_path(op, &op->path_a, from);
    if (early)
        return early;
    early = op_dup_path(op, &op->path_b, to);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_copy_opt(Mel_Fs* fs, str8 from, str8 to, Mel_Fs_Copy_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_COPY, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_COPY, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->overwrite = opt.overwrite;
    op->atomic = opt.atomic;
    Mel_Future* early = op_dup_path(op, &op->path_a, from);
    if (early)
        return early;
    early = op_dup_path(op, &op->path_b, to);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_read_file_opt(Mel_Fs* fs, str8 path, Mel_Fs_Read_File_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_READ_FILE, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_READ_FILE, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    Mel_Future* early = op_dup_path(op, &op->path_a, path);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_write_file_opt(Mel_Fs* fs, str8 path, Mel_Fs_Write_File_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_WRITE_FILE, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_WRITE_FILE, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->create_parents = opt.create_parents;
    op->atomic = opt.atomic;
    op->mode_bits = opt.mode_bits;
    op->write_len = opt.len;
    if (opt.len > 0)
    {
        u8* copy = mel_alloc(op->alloc, opt.len);
        if (!copy)
        {
            mel_fs__op_settle(op, MEL_FS_ERROR, ENOMEM);
            return &op->future;
        }
        memcpy(copy, opt.data, opt.len);
        op->write_data = copy;
    }
    Mel_Future* early = op_dup_path(op, &op->path_a, path);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_enumerate_opt(Mel_Fs* fs, str8 path, Mel_Fs_Enumerate_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_ENUMERATE, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_ENUMERATE, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->stat_entries = opt.stat_entries;
    op->batch = opt.batch;
    op->on_batch = opt.on_batch;
    op->stream_user = opt.stream_user;
    Mel_Future* early = op_dup_path(op, &op->path_a, path);
    if (early)
        return early;
    return op_submit(op);
}

Mel_Future* mel_fs_glob_opt(Mel_Fs* fs, str8 root, str8 pattern, Mel_Fs_Glob_Opt opt)
{
    if (!fs)
        return NULL;
    assert(mel_reactor_is_owner(fs->reactor));
    if (!fs->backend_ready)
        return op_fail_unavailable(fs, MEL_FS_JOB_GLOB, opt.deliver, opt.out_op);

    Mel_Fs_Op_Record* op = op_begin(fs, MEL_FS_JOB_GLOB, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->case_insensitive = opt.case_insensitive;
    op->recursive = opt.recursive;
    Mel_Future* early = op_dup_path(op, &op->path_a, root);
    if (early)
        return early;
    early = op_dup_path(op, &op->glob_pattern, pattern);
    if (early)
        return early;
    return op_submit(op);
}

bool mel_fs_cancel(Mel_Fs* fs, Mel_Fs_Op handle)
{
    if (!fs)
        return false;
    assert(mel_reactor_is_owner(fs->reactor));
    Mel_SlotMap_Handle h = mel_slotmap_handle_make(handle.index, handle.generation);
    Mel_Fs_Op_Record** pp = (Mel_Fs_Op_Record**)mel_slotmap_get(&fs->ops, h);
    Mel_Fs_Op_Record*  op = pp ? *pp : NULL;
    if (!op || op->settled)
        return false;
    op->cancel_requested = true;
    if (!op->submitted)
        mel_fs__op_settle(op, MEL_FS_ERROR | MEL_FS_CANCELLED, 0);
    return true;
}

static Mel_Fs_Op_Record* op_of_future(Mel_Future* f) { return mel_container_of(f, Mel_Fs_Op_Record, future); }

const Mel_Fs_Stat_Result*  mel_fs_future_stat(Mel_Future* f) { return f ? &op_of_future(f)->result.stat : NULL; }
const Mel_Fs_Bool_Result*  mel_fs_future_bool(Mel_Future* f) { return f ? &op_of_future(f)->result.boolean : NULL; }
const Mel_Fs_Void_Result*  mel_fs_future_void(Mel_Future* f) { return f ? &op_of_future(f)->result.voided : NULL; }
const Mel_Fs_Path_Result*  mel_fs_future_path(Mel_Future* f) { return f ? &op_of_future(f)->result.path : NULL; }
const Mel_Fs_Bytes_Result* mel_fs_future_bytes(Mel_Future* f) { return f ? &op_of_future(f)->result.bytes : NULL; }
const Mel_Fs_Dir_Result*   mel_fs_future_dir(Mel_Future* f) { return f ? &op_of_future(f)->result.dir : NULL; }

void mel_fs_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Mel_Fs_Op_Record* op = op_of_future(f);
    const Mel_Alloc*  a = op->alloc;
    if (op->kind == MEL_FS_JOB_READ_FILE && op->result.bytes.data)
        mel_dealloc(a, op->result.bytes.data);
    if (op->kind == MEL_FS_JOB_WRITE_FILE && op->write_data)
        mel_dealloc(a, (void*)op->write_data);
    if ((op->kind == MEL_FS_JOB_ENUMERATE || op->kind == MEL_FS_JOB_GLOB) && op->result.dir.entries)
    {
        for (u32 i = 0; i < op->result.dir.count; i++)
            if (op->result.dir.entries[i].name.data)
                mel_dealloc(a, op->result.dir.entries[i].name.data);
        mel_dealloc(a, op->result.dir.entries);
    }
    mel_dealloc(a, op);
}
