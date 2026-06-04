#include "storage_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <collection.list/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <reactor/reactor.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

static Mel_Future_Status future_status_from(Mel_Storage_Status status)
{
    Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
    if (status & MEL_STORAGE_PARTIAL)
        fs |= MEL_FUTURE_PARTIAL;
    return fs;
}

bool mel_storage_path_valid(str8 rel)
{
    if (rel.len == 0)
        return true;
    if (rel.data[0] == '/' || rel.data[0] == '\\')
        return false;
    if (rel.len >= 2 && ((rel.data[0] >= 'A' && rel.data[0] <= 'Z') || (rel.data[0] >= 'a' && rel.data[0] <= 'z')) && rel.data[1] == ':')
        return false;

    usize seg_start = 0;
    for (usize i = 0; i <= rel.len; i++)
    {
        bool boundary = i == rel.len || rel.data[i] == '/';
        if (rel.data && i < rel.len && rel.data[i] == '\\')
            return false;
        if (rel.data && i < rel.len && rel.data[i] == 0)
            return false;
        if (boundary)
        {
            usize seg_len = i - seg_start;
            if (seg_len == 0 && i != rel.len && seg_start == i)
            {
                if (i == 0)
                    return false;
            }
            if (seg_len == 2 && rel.data[seg_start] == '.' && rel.data[seg_start + 1] == '.')
                return false;
            seg_start = i + 1;
        }
    }
    return true;
}

static void result_set_status(Mel_Storage_Job* job, Mel_Storage_Status status)
{
    switch (job->kind)
    {
    case MEL_STORAGE_JOB_READ:
        job->result.bytes.status = status;
        break;
    case MEL_STORAGE_JOB_SIZE:
        job->result.size.status = status;
        break;
    case MEL_STORAGE_JOB_META:
        job->result.meta.status = status;
        break;
    case MEL_STORAGE_JOB_SPACE:
        job->result.space.status = status;
        break;
    case MEL_STORAGE_JOB_ENUMERATE:
    case MEL_STORAGE_JOB_GLOB:
        job->result.list.status = status;
        break;
    default:
        job->result.voided.status = status;
        break;
    }
}

static void job_free_owned(Mel_Storage_Job* job)
{
    const Mel_Alloc* a = job->alloc;
    if (job->path_a.data)
        mel_dealloc(a, job->path_a.data);
    if (job->path_b.data)
        mel_dealloc(a, job->path_b.data);
    if (job->pattern.data)
        mel_dealloc(a, job->pattern.data);
    job->path_a = STR8_EMPTY;
    job->path_b = STR8_EMPTY;
    job->pattern = STR8_EMPTY;
}

void mel_storage__job_free_record(Mel_Storage_Job* job)
{
    const Mel_Alloc* a = job->alloc;
    if (job->kind == MEL_STORAGE_JOB_READ && job->result.bytes.data)
        mel_dealloc(a, job->result.bytes.data);
    if (job->kind == MEL_STORAGE_JOB_WRITE && job->write_data)
        mel_dealloc(a, (void*)job->write_data);
    if ((job->kind == MEL_STORAGE_JOB_ENUMERATE || job->kind == MEL_STORAGE_JOB_GLOB) && job->result.list.entries)
    {
        for (u32 i = 0; i < job->result.list.count; i++)
            if (job->result.list.entries[i].name.data)
                mel_dealloc(a, job->result.list.entries[i].name.data);
        mel_dealloc(a, job->result.list.entries);
    }
    mel_dealloc(a, job);
}

void mel_storage__job_settle(Mel_Storage_Job* job, Mel_Storage_Status status)
{
    if (job->settled)
        return;
    assert(job->orphaned || mel_reactor_is_owner(job->st->reactor));
    job->settled = true;

    result_set_status(job, status);
    job_free_owned(job);

    if (!job->orphaned && mel_slotmap_alive(&job->st->ops, job->self))
        mel_slotmap_remove(&job->st->ops, job->self);

    if (status & MEL_STORAGE_CANCELLED)
        mel_future_cancel(&job->future);
    else
        mel_future_resolve(&job->future, &job->result, future_status_from(status));
}

static Mel_Storage* storage_alloc(Mel_Storage_Opt opt, const Mel_Storage_Interface* iface, void* backend_user)
{
    if (!opt.reactor)
    {
        mel_log_error("storage", "create: reactor is required");
        return NULL;
    }
    if (!iface)
    {
        mel_log_error("storage", "create: backend interface is required");
        return NULL;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Storage*     st = mel_alloc_type(alloc, Mel_Storage);
    if (!st)
        return NULL;
    memset(st, 0, sizeof *st);

    st->reactor = opt.reactor;
    st->executor = mel_reactor_executor(opt.reactor);
    st->alloc = alloc;
    st->iface = iface;
    st->backend_user = backend_user;
    st->writable = opt.writable;

    mel_slotmap_init(&st->ops, alloc, .item_size = sizeof(Mel_Storage_Job*), .initial_capacity = 16);
    return st;
}

Mel_Storage* mel_storage_create_opt(Mel_Storage_Opt opt)
{
    if (!opt.iface)
    {
        mel_log_error("storage", "create: an explicit interface is required; use open_title/open_user/open_fs for the built-in backends");
        return NULL;
    }
    return storage_alloc(opt, opt.iface, opt.backend_user);
}

void mel_storage_destroy(Mel_Storage* st)
{
    if (!st)
        return;
    assert(mel_reactor_is_owner(st->reactor));

    while (mel_slotmap_count(&st->ops) > 0)
    {
        Mel_Storage_Job** data = (Mel_Storage_Job**)mel_slotmap_data(&st->ops);
        Mel_Storage_Job*  job = data[0];
        job->cancel_requested = true;
        if (st->iface->cancel)
            st->iface->cancel(st, st->backend_user, job);
        if (job->settled)
            continue;
        bool has_backend_cont = job->submitted && job->backend_pending != NULL;
        job->orphaned = has_backend_cont;
        mel_slotmap_remove(&st->ops, job->self);
        mel_storage__job_settle(job, MEL_STORAGE_ERROR | MEL_STORAGE_CANCELLED);
        if (!has_backend_cont)
            mel_storage__job_free_record(job);
    }

    if (st->iface->destroy)
        st->iface->destroy(st, st->backend_user);

    mel_slotmap_free(&st->ops);
    const Mel_Alloc* alloc = st->alloc;
    mel_dealloc(alloc, st);
}

bool mel_storage_ready(const Mel_Storage* st)
{
    if (!st)
        return false;
    if (!st->iface->ready)
        return true;
    return st->iface->ready((Mel_Storage*)st, st->backend_user);
}

bool          mel_storage_writable(const Mel_Storage* st) { return st && st->writable; }
Mel_Reactor*  mel_storage_reactor(const Mel_Storage* st) { return st ? st->reactor : NULL; }
Mel_Executor* mel_storage_executor(const Mel_Storage* st) { return st ? st->executor : NULL; }
u32           mel_storage_pending(const Mel_Storage* st) { return st ? mel_slotmap_count((Mel_SlotMap*)&st->ops) : 0; }

static Mel_Storage_Job* job_begin(Mel_Storage* st, Mel_Storage_Job_Kind kind, Mel_Executor* deliver, Mel_Storage_Op* out_op)
{
    const Mel_Alloc* alloc = st->alloc;
    Mel_Storage_Job* job = mel_alloc_type(alloc, Mel_Storage_Job);
    if (!job)
        return NULL;
    memset(job, 0, sizeof *job);

    mel_future_init(&job->future, NULL, alloc);
    job->future.value = &job->result;

    job->st = st;
    job->alloc = alloc;
    job->kind = kind;
    job->deliver = deliver ? deliver : st->executor;

    Mel_Storage_Job* slot = job;
    job->self = mel_slotmap_insert(&st->ops, &slot);
    if (out_op)
        *out_op = (Mel_Storage_Op){ .index = job->self.index, .generation = job->self.generation };
    return job;
}

static Mel_Future* job_dup_path(Mel_Storage_Job* job, str8* dst, str8 src)
{
    *dst = str8_dup_alloc(src, job->alloc);
    if (src.len > 0 && dst->data == NULL)
    {
        mel_storage__job_settle(job, MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE);
        return &job->future;
    }
    return NULL;
}

static Mel_Future* job_reject(Mel_Storage* st, Mel_Storage_Job_Kind kind, Mel_Executor* deliver, Mel_Storage_Op* out_op, Mel_Storage_Status status)
{
    Mel_Storage_Job* job = job_begin(st, kind, deliver, out_op);
    if (!job)
        return NULL;
    mel_storage__job_settle(job, status);
    return &job->future;
}

static Mel_Future* job_submit(Mel_Storage_Job* job)
{
    job->submitted = true;
    job->st->iface->submit(job->st, job->st->backend_user, job);
    return &job->future;
}

static bool check_path(str8 rel)
{
    if (!mel_storage_path_valid(rel))
    {
        mel_log_error("storage", "rejected path '%.*s': must be a relative UTF-8 forward-slash path without '..' or drive prefix", (int)rel.len, rel.data);
        return false;
    }
    return true;
}

Mel_Future* mel_storage_read_opt(Mel_Storage* st, str8 rel, Mel_Storage_Read_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(rel))
        return job_reject(st, MEL_STORAGE_JOB_READ, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_READ, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_READ, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    job->read_expect = opt.expect;
    Mel_Future* early = job_dup_path(job, &job->path_a, rel);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_write_opt(Mel_Storage* st, str8 rel, Mel_Storage_Write_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(rel))
        return job_reject(st, MEL_STORAGE_JOB_WRITE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!st->writable)
        return job_reject(st, MEL_STORAGE_JOB_WRITE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_READ_ONLY);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_WRITE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_WRITE, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    job->create_parents = opt.create_parents;
    job->atomic = opt.atomic;
    job->write_len = opt.len;
    if (opt.len > 0)
    {
        u8* copy = mel_alloc(job->alloc, opt.len);
        if (!copy)
        {
            mel_storage__job_settle(job, MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE);
            return &job->future;
        }
        memcpy(copy, opt.data, opt.len);
        job->write_data = copy;
    }
    Mel_Future* early = job_dup_path(job, &job->path_a, rel);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_size_opt(Mel_Storage* st, str8 rel, Mel_Storage_Size_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(rel))
        return job_reject(st, MEL_STORAGE_JOB_SIZE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_SIZE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_SIZE, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    Mel_Future* early = job_dup_path(job, &job->path_a, rel);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_meta_opt(Mel_Storage* st, str8 rel, Mel_Storage_Meta_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(rel))
        return job_reject(st, MEL_STORAGE_JOB_META, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_META, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_META, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    Mel_Future* early = job_dup_path(job, &job->path_a, rel);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_enumerate_opt(Mel_Storage* st, str8 rel, Mel_Storage_Enum_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(rel))
        return job_reject(st, MEL_STORAGE_JOB_ENUMERATE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_ENUMERATE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_ENUMERATE, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    job->stat_entries = opt.stat_entries;
    job->batch = opt.batch;
    job->on_batch = opt.on_batch;
    job->stream_user = opt.stream_user;
    Mel_Future* early = job_dup_path(job, &job->path_a, rel);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_glob_opt(Mel_Storage* st, str8 rel, str8 pattern, Mel_Storage_Glob_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(rel))
        return job_reject(st, MEL_STORAGE_JOB_GLOB, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_GLOB, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_GLOB, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    job->case_insensitive = opt.case_insensitive;
    job->recursive = opt.recursive;
    Mel_Future* early = job_dup_path(job, &job->path_a, rel);
    if (early)
        return early;
    early = job_dup_path(job, &job->pattern, pattern);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_mkdir_opt(Mel_Storage* st, str8 rel, Mel_Storage_Mkdir_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(rel))
        return job_reject(st, MEL_STORAGE_JOB_MKDIR, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!st->writable)
        return job_reject(st, MEL_STORAGE_JOB_MKDIR, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_READ_ONLY);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_MKDIR, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_MKDIR, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    job->parents = opt.parents;
    Mel_Future* early = job_dup_path(job, &job->path_a, rel);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_remove_opt(Mel_Storage* st, str8 rel, Mel_Storage_Remove_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(rel))
        return job_reject(st, MEL_STORAGE_JOB_REMOVE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (rel.len == 0)
        return job_reject(st, MEL_STORAGE_JOB_REMOVE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH);
    if (!st->writable)
        return job_reject(st, MEL_STORAGE_JOB_REMOVE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_READ_ONLY);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_REMOVE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_REMOVE, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    job->recursive = opt.recursive;
    Mel_Future* early = job_dup_path(job, &job->path_a, rel);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_rename_opt(Mel_Storage* st, str8 from, str8 to, Mel_Storage_Rename_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(from) || !check_path(to) || from.len == 0 || to.len == 0)
        return job_reject(st, MEL_STORAGE_JOB_RENAME, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!st->writable)
        return job_reject(st, MEL_STORAGE_JOB_RENAME, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_READ_ONLY);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_RENAME, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_RENAME, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    job->overwrite = opt.overwrite;
    Mel_Future* early = job_dup_path(job, &job->path_a, from);
    if (early)
        return early;
    early = job_dup_path(job, &job->path_b, to);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_copy_opt(Mel_Storage* st, str8 from, str8 to, Mel_Storage_Copy_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!check_path(from) || !check_path(to) || from.len == 0 || to.len == 0)
        return job_reject(st, MEL_STORAGE_JOB_COPY, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_BAD_PATH | MEL_STORAGE_ESCAPE);
    if (!st->writable)
        return job_reject(st, MEL_STORAGE_JOB_COPY, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_READ_ONLY);
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_COPY, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_COPY, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    job->overwrite = opt.overwrite;
    job->atomic = opt.atomic;
    Mel_Future* early = job_dup_path(job, &job->path_a, from);
    if (early)
        return early;
    early = job_dup_path(job, &job->path_b, to);
    if (early)
        return early;
    return job_submit(job);
}

Mel_Future* mel_storage_space_opt(Mel_Storage* st, Mel_Storage_Space_Opt opt)
{
    if (!st)
        return NULL;
    assert(mel_reactor_is_owner(st->reactor));
    if (!mel_storage_ready(st))
        return job_reject(st, MEL_STORAGE_JOB_SPACE, opt.deliver, opt.out_op, MEL_STORAGE_ERROR | MEL_STORAGE_NOT_READY);

    Mel_Storage_Job* job = job_begin(st, MEL_STORAGE_JOB_SPACE, opt.deliver, opt.out_op);
    if (!job)
        return NULL;
    return job_submit(job);
}

bool mel_storage_cancel(Mel_Storage* st, Mel_Storage_Op op)
{
    if (!st)
        return false;
    assert(mel_reactor_is_owner(st->reactor));
    Mel_SlotMap_Handle h = mel_slotmap_handle_make(op.index, op.generation);
    Mel_Storage_Job**  pp = (Mel_Storage_Job**)mel_slotmap_get(&st->ops, h);
    Mel_Storage_Job*   job = pp ? *pp : NULL;
    if (!job || job->settled)
        return false;
    job->cancel_requested = true;
    if (st->iface->cancel)
        st->iface->cancel(st, st->backend_user, job);
    if (!job->submitted)
        mel_storage__job_settle(job, MEL_STORAGE_ERROR | MEL_STORAGE_CANCELLED);
    return true;
}

static Mel_Storage_Job* job_of_future(Mel_Future* f) { return mel_container_of(f, Mel_Storage_Job, future); }

const Mel_Storage_Bytes*        mel_storage_future_bytes(Mel_Future* f) { return f ? &job_of_future(f)->result.bytes : NULL; }
const Mel_Storage_Size_Result*  mel_storage_future_size(Mel_Future* f) { return f ? &job_of_future(f)->result.size : NULL; }
const Mel_Storage_Void_Result*  mel_storage_future_void(Mel_Future* f) { return f ? &job_of_future(f)->result.voided : NULL; }
const Mel_Storage_Meta_Result*  mel_storage_future_meta(Mel_Future* f) { return f ? &job_of_future(f)->result.meta : NULL; }
const Mel_Storage_Space_Result* mel_storage_future_space(Mel_Future* f) { return f ? &job_of_future(f)->result.space : NULL; }
const Mel_Storage_List_Result*  mel_storage_future_list(Mel_Future* f) { return f ? &job_of_future(f)->result.list : NULL; }

void mel_storage_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Mel_Storage_Job* job = job_of_future(f);
    if (job->orphaned)
        return;
    mel_storage__job_free_record(job);
}

Mel_Storage_Job_Kind mel_storage_job_kind(const Mel_Storage_Job* job) { return job->kind; }
str8                 mel_storage_job_path(const Mel_Storage_Job* job) { return job->path_a; }
str8                 mel_storage_job_path_b(const Mel_Storage_Job* job) { return job->path_b; }
str8                 mel_storage_job_pattern(const Mel_Storage_Job* job) { return job->pattern; }
const u8*            mel_storage_job_write_data(const Mel_Storage_Job* job) { return job->write_data; }
usize                mel_storage_job_write_len(const Mel_Storage_Job* job) { return job->write_len; }
usize                mel_storage_job_read_expect(const Mel_Storage_Job* job) { return job->read_expect; }
const Mel_Alloc*     mel_storage_job_alloc(const Mel_Storage_Job* job) { return job->alloc; }
Mel_Reactor*         mel_storage_job_reactor(const Mel_Storage_Job* job) { return job->st->reactor; }
Mel_Executor*        mel_storage_job_deliver(const Mel_Storage_Job* job) { return job->deliver; }
bool                 mel_storage_job_create_parents(const Mel_Storage_Job* job) { return job->create_parents; }
bool                 mel_storage_job_atomic(const Mel_Storage_Job* job) { return job->atomic; }
bool                 mel_storage_job_parents(const Mel_Storage_Job* job) { return job->parents; }
bool                 mel_storage_job_recursive(const Mel_Storage_Job* job) { return job->recursive; }
bool                 mel_storage_job_overwrite(const Mel_Storage_Job* job) { return job->overwrite; }
bool                 mel_storage_job_case_insensitive(const Mel_Storage_Job* job) { return job->case_insensitive; }
bool                 mel_storage_job_stat_entries(const Mel_Storage_Job* job) { return job->stat_entries; }
u32                  mel_storage_job_batch(const Mel_Storage_Job* job) { return job->batch; }
Mel_Storage_Enum_Cb  mel_storage_job_on_batch(const Mel_Storage_Job* job) { return job->on_batch; }
void*                mel_storage_job_stream_user(const Mel_Storage_Job* job) { return job->stream_user; }

void mel_storage_job_settle_bytes(Mel_Storage_Job* job, u8* data, usize len, Mel_Storage_Status status)
{
    job->result.bytes.data = data;
    job->result.bytes.len = len;
    mel_storage__job_settle(job, status);
}

void mel_storage_job_settle_size(Mel_Storage_Job* job, u64 value, Mel_Storage_Status status)
{
    job->result.size.value = value;
    mel_storage__job_settle(job, status);
}

void mel_storage_job_settle_void(Mel_Storage_Job* job, Mel_Storage_Status status)
{
    mel_storage__job_settle(job, status);
}

void mel_storage_job_settle_meta(Mel_Storage_Job* job, Mel_Storage_Meta value, Mel_Storage_Status status)
{
    job->result.meta.value = value;
    mel_storage__job_settle(job, status);
}

void mel_storage_job_settle_space(Mel_Storage_Job* job, Mel_Storage_Space value, Mel_Storage_Status status)
{
    job->result.space.value = value;
    mel_storage__job_settle(job, status);
}

void mel_storage_job_settle_list(Mel_Storage_Job* job, Mel_Storage_Entry* entries, u32 count, Mel_Storage_Status status)
{
    job->result.list.entries = entries;
    job->result.list.count = count;
    mel_storage__job_settle(job, status);
}
