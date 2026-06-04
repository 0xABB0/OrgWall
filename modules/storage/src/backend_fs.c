#include "storage_internal.h"

#include <storage/backend.h>

#include <fs/fs.h>
#include <fs/dir.h>
#include <fs/paths.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <future/future.h>
#include <reactor/reactor.h>
#include <string/str8.h>
#include <string/path.h>
#include <collection.list/list.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    Mel_Fs*          fs;
    str8             root;
    bool             owns_fs;
    bool             create_root;
    const Mel_Alloc* alloc;
} Fs_Backend;


Mel_Storage_Space mel_storage__native_space(str8 host_root);

static Mel_Storage_Status status_from_fs(Mel_Fs_Status s)
{
    Mel_Storage_Status out = s & MEL_STORAGE_SEVERITY_MASK;
    if (s & MEL_FS_CANCELLED)
        out |= MEL_STORAGE_CANCELLED;
    if (s & MEL_FS_NOT_FOUND)
        out |= MEL_STORAGE_NOT_FOUND;
    if (s & MEL_FS_EXISTS)
        out |= MEL_STORAGE_EXISTS;
    if (s & MEL_FS_PERMISSION)
        out |= MEL_STORAGE_PERMISSION;
    if (s & MEL_FS_NOT_A_DIRECTORY)
        out |= MEL_STORAGE_NOT_A_DIRECTORY;
    if (s & MEL_FS_IS_A_DIRECTORY)
        out |= MEL_STORAGE_IS_A_DIRECTORY;
    if (s & MEL_FS_NOT_EMPTY)
        out |= MEL_STORAGE_NOT_EMPTY;
    if (s & MEL_FS_NO_SPACE)
        out |= MEL_STORAGE_NO_SPACE;
    if (s & MEL_FS_NAME_TOO_LONG)
        out |= MEL_STORAGE_NAME_TOO_LONG;
    if (s & MEL_FS_PARTIAL)
        out |= MEL_STORAGE_PARTIAL;
    if (s & MEL_FS_CROSS_DEVICE)
        out |= MEL_STORAGE_CROSS_DEVICE;
    if (s & MEL_FS_READ_ONLY)
        out |= MEL_STORAGE_READ_ONLY;
    if (s & MEL_FS_UNAVAILABLE)
        out |= MEL_STORAGE_UNAVAILABLE;
    return out;
}

static Mel_Storage_Kind kind_from_fs(Mel_Fs_Kind k)
{
    Mel_Storage_Kind out = MEL_STORAGE_KIND_NONE;
    if (k & MEL_FS_KIND_FILE)
        out |= MEL_STORAGE_KIND_FILE;
    if (k & MEL_FS_KIND_DIR)
        out |= MEL_STORAGE_KIND_DIR;
    return out;
}

static str8 host_path(Fs_Backend* b, str8 rel, u8* buf, usize cap)
{
    return mel_path_join(b->root, rel, buf, cap);
}

static bool fs_ready(Mel_Storage* st, void* user)
{
    (void)st;
    Fs_Backend* b = (Fs_Backend*)user;
    return b->fs && mel_fs_available(b->fs);
}

typedef struct
{
    Mel_Storage_Job* job;
    Mel_Fs*          fs;
} Chain;

static bool chain_orphaned(Mel_Storage_Job* job)
{
    if (!job->orphaned)
        return false;
    mel_fs_future_release(job->backend_pending);
    job->backend_pending = NULL;
    mel_storage__job_free_record(job);
    return true;
}

static void chain_read(Mel_Task* self)
{
    Mel_Storage_Job*           job = mel_container_of(self, Mel_Storage_Job, backend_task);
    if (chain_orphaned(job))
        return;
    const Mel_Fs_Bytes_Result* r = mel_fs_future_bytes(job->backend_pending);
    Mel_Storage_Status         st = status_from_fs(r->status);
    if (mel_storage_failed(st))
    {
        mel_fs_future_release(job->backend_pending);
        job->backend_pending = NULL;
        mel_storage_job_settle_bytes(job, NULL, 0, st);
        return;
    }
    usize len = r->len;
    if (job->read_expect != 0 && len != job->read_expect)
    {
        mel_fs_future_release(job->backend_pending);
        job->backend_pending = NULL;
        mel_storage_job_settle_bytes(job, NULL, 0, MEL_STORAGE_ERROR | MEL_STORAGE_SIZE_MISMATCH);
        return;
    }
    u8* data = NULL;
    if (len > 0)
    {
        data = mel_alloc(job->alloc, len);
        if (!data)
        {
            mel_fs_future_release(job->backend_pending);
            job->backend_pending = NULL;
            mel_storage_job_settle_bytes(job, NULL, 0, MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE);
            return;
        }
        memcpy(data, r->data, len);
    }
    mel_fs_future_release(job->backend_pending);
    job->backend_pending = NULL;
    mel_storage_job_settle_bytes(job, data, len, MEL_STORAGE_OK);
}

static void chain_size(Mel_Task* self)
{
    Mel_Storage_Job*          job = mel_container_of(self, Mel_Storage_Job, backend_task);
    if (chain_orphaned(job))
        return;
    const Mel_Fs_Stat_Result* r = mel_fs_future_stat(job->backend_pending);
    Mel_Storage_Status        st = status_from_fs(r->status);
    if (!mel_storage_failed(st) && !r->value.exists)
        st = MEL_STORAGE_ERROR | MEL_STORAGE_NOT_FOUND;
    u64 size = r->value.size_bytes;
    mel_fs_future_release(job->backend_pending);
    job->backend_pending = NULL;
    mel_storage_job_settle_size(job, size, st);
}

static void chain_meta(Mel_Task* self)
{
    Mel_Storage_Job*          job = mel_container_of(self, Mel_Storage_Job, backend_task);
    if (chain_orphaned(job))
        return;
    const Mel_Fs_Stat_Result* r = mel_fs_future_stat(job->backend_pending);
    Mel_Storage_Status        st = status_from_fs(r->status);
    Mel_Storage_Meta          m = { 0 };
    m.exists = r->value.exists;
    m.kind = kind_from_fs(r->value.kind);
    m.size_bytes = r->value.size_bytes;
    m.mtime_ns = r->value.mtime_ns;
    m.read_only = r->value.read_only;
    mel_fs_future_release(job->backend_pending);
    job->backend_pending = NULL;
    mel_storage_job_settle_meta(job, m, st);
}

static void chain_void(Mel_Task* self)
{
    Mel_Storage_Job*          job = mel_container_of(self, Mel_Storage_Job, backend_task);
    if (chain_orphaned(job))
        return;
    const Mel_Fs_Void_Result* r = mel_fs_future_void(job->backend_pending);
    Mel_Storage_Status        st = status_from_fs(r->status);
    mel_fs_future_release(job->backend_pending);
    job->backend_pending = NULL;
    mel_storage_job_settle_void(job, st);
}

static void chain_list(Mel_Task* self)
{
    Mel_Storage_Job*         job = mel_container_of(self, Mel_Storage_Job, backend_task);
    if (chain_orphaned(job))
        return;
    const Mel_Fs_Dir_Result* r = mel_fs_future_dir(job->backend_pending);
    Mel_Storage_Status       st = status_from_fs(r->status);
    if (mel_storage_failed(st))
    {
        mel_fs_future_release(job->backend_pending);
        job->backend_pending = NULL;
        mel_storage_job_settle_list(job, NULL, 0, st);
        return;
    }
    Mel_Storage_Entry* entries = NULL;
    if (r->count > 0)
    {
        entries = mel_alloc_array(job->alloc, Mel_Storage_Entry, r->count);
        if (!entries)
        {
            mel_fs_future_release(job->backend_pending);
            job->backend_pending = NULL;
            mel_storage_job_settle_list(job, NULL, 0, MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE);
            return;
        }
        for (u32 i = 0; i < r->count; i++)
        {
            entries[i].name = str8_dup_alloc(r->entries[i].name, job->alloc);
            entries[i].kind = kind_from_fs(r->entries[i].kind);
            entries[i].size_bytes = r->entries[i].size_bytes;
            entries[i].mtime_ns = r->entries[i].mtime_ns;
        }
    }
    u32 count = r->count;
    mel_fs_future_release(job->backend_pending);
    job->backend_pending = NULL;
    mel_storage_job_settle_list(job, entries, count, MEL_STORAGE_OK);
}

static bool arm(Mel_Storage_Job* job, Mel_Fs* fs, Mel_Future* f, void (*cont)(Mel_Task*))
{
    if (!f)
        return false;
    job->backend_pending = f;
    mel_task_init(&job->backend_task, cont);
    Mel_Executor* exec = mel_fs_executor(fs);
    mel_future_then(f, &job->backend_task, exec ? exec : mel_executor_inline());
    return true;
}

static void enum_stream_trampoline(const Mel_Fs_Dir_Entry* entries, u32 count, void* user)
{
    Mel_Storage_Job* job = (Mel_Storage_Job*)user;
    if (!job->on_batch || count == 0)
        return;
    Mel_Storage_Entry* tmp = mel_alloc_array(job->alloc, Mel_Storage_Entry, count);
    if (!tmp)
        return;
    for (u32 i = 0; i < count; i++)
    {
        tmp[i].name = entries[i].name;
        tmp[i].kind = kind_from_fs(entries[i].kind);
        tmp[i].size_bytes = entries[i].size_bytes;
        tmp[i].mtime_ns = entries[i].mtime_ns;
    }
    job->on_batch(tmp, count, job->stream_user);
    mel_dealloc(job->alloc, tmp);
}

static void fs_submit(Mel_Storage* st, void* user, Mel_Storage_Job* job)
{
    Fs_Backend* b = (Fs_Backend*)user;
    (void)st;

    if (job->cancel_requested)
    {
        mel_storage_job_settle_void(job, MEL_STORAGE_ERROR | MEL_STORAGE_CANCELLED);
        return;
    }

    if (job->kind == MEL_STORAGE_JOB_SPACE)
    {
        Mel_Storage_Space sp = mel_storage__native_space(b->root);
        if (sp.total_bytes == 0 && sp.free_bytes == 0 && sp.available_bytes == 0)
            mel_storage_job_settle_space(job, sp, MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE);
        else
            mel_storage_job_settle_space(job, sp, MEL_STORAGE_OK);
        return;
    }

    u8   buf_a[4096];
    u8   buf_b[4096];
    str8 ha = host_path(b, job->path_a, buf_a, sizeof buf_a);

    Mel_Future*   f = NULL;
    bool          armed = false;
    Mel_Executor* deliver = mel_fs_executor(b->fs);

    switch (job->kind)
    {
    case MEL_STORAGE_JOB_READ:
        f = mel_fs_read_file_opt(b->fs, ha, (Mel_Fs_Read_File_Opt){ .deliver = deliver });
        armed = arm(job, b->fs, f, chain_read);
        break;
    case MEL_STORAGE_JOB_WRITE:
        f = mel_fs_write_file_opt(b->fs, ha, (Mel_Fs_Write_File_Opt){ .data = job->write_data, .len = job->write_len, .create_parents = job->create_parents, .atomic = job->atomic, .mode_bits = MEL_FS_MODE_DEFAULT_FILE, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_void);
        break;
    case MEL_STORAGE_JOB_SIZE:
        f = mel_fs_stat_opt(b->fs, ha, (Mel_Fs_Stat_Opt){ .follow_symlinks = true, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_size);
        break;
    case MEL_STORAGE_JOB_META:
        f = mel_fs_stat_opt(b->fs, ha, (Mel_Fs_Stat_Opt){ .follow_symlinks = true, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_meta);
        break;
    case MEL_STORAGE_JOB_ENUMERATE:
        f = mel_fs_enumerate_opt(b->fs, ha, (Mel_Fs_Enumerate_Opt){ .stat_entries = job->stat_entries, .batch = job->batch ? job->batch : 64, .on_batch = job->on_batch ? enum_stream_trampoline : NULL, .stream_user = job, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_list);
        break;
    case MEL_STORAGE_JOB_GLOB:
        f = mel_fs_glob_opt(b->fs, ha, job->pattern, (Mel_Fs_Glob_Opt){ .case_insensitive = job->case_insensitive, .recursive = job->recursive, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_list);
        break;
    case MEL_STORAGE_JOB_MKDIR:
        f = mel_fs_mkdir_opt(b->fs, ha, (Mel_Fs_Mkdir_Opt){ .mode_bits = MEL_FS_MODE_DEFAULT_DIR, .parents = job->parents, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_void);
        break;
    case MEL_STORAGE_JOB_REMOVE:
        f = mel_fs_remove_opt(b->fs, ha, (Mel_Fs_Remove_Opt){ .recursive = job->recursive, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_void);
        break;
    case MEL_STORAGE_JOB_RENAME:
    {
        str8 hb = host_path(b, job->path_b, buf_b, sizeof buf_b);
        f = mel_fs_rename_opt(b->fs, ha, hb, (Mel_Fs_Rename_Opt){ .overwrite = job->overwrite, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_void);
        break;
    }
    case MEL_STORAGE_JOB_COPY:
    {
        str8 hb = host_path(b, job->path_b, buf_b, sizeof buf_b);
        f = mel_fs_copy_opt(b->fs, ha, hb, (Mel_Fs_Copy_Opt){ .overwrite = job->overwrite, .atomic = job->atomic, .deliver = deliver });
        armed = arm(job, b->fs, f, chain_void);
        break;
    }
    default:
        mel_storage_job_settle_void(job, MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE);
        return;
    }

    if (!armed)
        mel_storage_job_settle_void(job, MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE);
}

static void fs_destroy(Mel_Storage* st, void* user)
{
    (void)st;
    Fs_Backend* b = (Fs_Backend*)user;
    if (!b)
        return;
    const Mel_Alloc* a = b->alloc;
    if (b->owns_fs && b->fs)
        mel_fs_destroy(b->fs);
    if (b->root.data)
        mel_dealloc(a, b->root.data);
    mel_dealloc(a, b);
}

static const Mel_Storage_Interface FS_IFACE = {
    .name = "fs",
    .ready = fs_ready,
    .submit = fs_submit,
    .cancel = NULL,
    .destroy = fs_destroy,
};

const Mel_Storage_Interface* mel_storage_fs_interface(void) { return &FS_IFACE; }

static Mel_Storage* open_with_root(str8 host_root, Mel_Reactor* reactor, const Mel_Alloc* alloc, bool writable, bool create_root, bool host_root_owned)
{
    alloc = alloc ? alloc : mel_alloc_heap();
    if (!reactor)
    {
        mel_log_error("storage", "open: reactor is required");
        if (host_root_owned && host_root.data)
            mel_dealloc(alloc, host_root.data);
        return NULL;
    }
    if (host_root.len == 0)
    {
        mel_log_error("storage", "open: could not resolve a host root for this storage scope");
        if (host_root_owned && host_root.data)
            mel_dealloc(alloc, host_root.data);
        return NULL;
    }

    Fs_Backend* b = mel_alloc_type(alloc, Fs_Backend);
    if (!b)
    {
        if (host_root_owned && host_root.data)
            mel_dealloc(alloc, host_root.data);
        return NULL;
    }
    memset(b, 0, sizeof *b);
    b->alloc = alloc;
    b->root = host_root_owned ? host_root : str8_dup_alloc(host_root, alloc);
    if (host_root.len > 0 && b->root.data == NULL)
    {
        mel_dealloc(alloc, b);
        return NULL;
    }
    b->create_root = create_root;
    b->fs = mel_fs_create(.reactor = reactor, .alloc = alloc);
    if (!b->fs)
    {
        mel_log_error("storage", "open: failed to create the filesystem driver");
        if (b->root.data)
            mel_dealloc(alloc, b->root.data);
        mel_dealloc(alloc, b);
        return NULL;
    }
    b->owns_fs = true;

    Mel_Storage* st = mel_storage_create_opt((Mel_Storage_Opt){ .reactor = reactor, .alloc = alloc, .writable = writable, .iface = &FS_IFACE, .backend_user = b });
    if (!st)
    {
        mel_fs_destroy(b->fs);
        if (b->root.data)
            mel_dealloc(alloc, b->root.data);
        mel_dealloc(alloc, b);
        return NULL;
    }

    if (writable && create_root)
        (void)mel_fs_mkdir_opt(b->fs, b->root, (Mel_Fs_Mkdir_Opt){ .mode_bits = MEL_FS_MODE_DEFAULT_DIR, .parents = true });

    return st;
}

Mel_Storage* mel_storage_open_fs_opt(Mel_Storage_Fs_Opt opt)
{
    if (opt.root.len == 0 || !mel_path_is_absolute(opt.root))
    {
        mel_log_error("storage", "open_fs: root must be an absolute host path");
        return NULL;
    }
    return open_with_root(opt.root, opt.reactor, opt.alloc, opt.writable, opt.create_root, false);
}

Mel_Storage* mel_storage__open_fs_folder(u32 folder, Mel_Reactor* reactor, const Mel_Alloc* alloc, bool writable, bool create_root)
{
    alloc = alloc ? alloc : mel_alloc_heap();
    Mel_Fs_Path_Result r = mel_fs_folder(folder, alloc);
    if (mel_fs_failed(r.status) || r.value.len == 0)
    {
        mel_log_error("storage", "open: standard folder %u is unavailable on this platform", folder);
        if (r.value.data)
            mel_dealloc(alloc, r.value.data);
        return NULL;
    }
    return open_with_root(r.value, reactor, alloc, writable, create_root, true);
}

Mel_Storage* mel_storage_open_user_opt(Mel_Storage_Opt opt)
{
    return mel_storage__open_fs_folder(MEL_FS_FOLDER_PREF, opt.reactor, opt.alloc, opt.writable, true);
}
