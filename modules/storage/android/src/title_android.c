#include "../../src/storage_internal.h"

#include <storage/backend.h>
#include <storage/android/android.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <future/future.h>
#include <vat/vat.h>
#include <thread/thread.h>
#include <thread/sem.h>
#include <collection/mpsc.h>
#include <collection/array.h>
#include <collection/list.h>
#include <string/str8.h>
#include <fs/dir.h>
#include <log/log.h>

#include <android/asset_manager.h>

#include <stdatomic.h>
#include <string.h>

static AAssetManager* g_assets;

void mel_storage_android_set_asset_manager(void* asset_manager) { g_assets = (AAssetManager*)asset_manager; }

bool mel_storage__title_native_available(void) { return g_assets != NULL; }

typedef struct
{
    Mel_Thread       worker;
    Mel_Mpsc         queue;
    Mel_Sem          items;
    _Atomic(u32)     running;
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
} Asset_Backend;

typedef struct
{
    Mel_Mpsc_Node      node;
    Mel_Task           completion_task;
    Mel_Storage_Job*   job;
    Asset_Backend*     backend;
    Mel_Storage_Status status;

    u8*                bytes;
    usize              bytes_len;
    u64                size;
    Mel_Storage_Meta   meta;
    Mel_Storage_Entry* entries;
    u32                entry_count;
} Asset_Task;

static const char* job_cpath(Mel_Storage_Job* job, str8 path) { return str8_to_cstr(path, mel_storage_job_alloc(job)); }

static bool asset_is_dir(const char* path)
{
    AAssetDir* d = AAssetManager_openDir(g_assets, path ? path : "");
    if (!d)
        return false;
    const char* first = AAssetDir_getNextFileName(d);
    bool        any = first != NULL;
    AAssetDir_close(d);
    return any;
}

static void work_read(Asset_Task* t)
{
    Mel_Storage_Job* job = t->job;
    const Mel_Alloc* alloc = mel_storage_job_alloc(job);
    const char*      path = job_cpath(job, mel_storage_job_path(job));
    AAsset*          a = path ? AAssetManager_open(g_assets, path, AASSET_MODE_BUFFER) : NULL;
    if (path)
        mel_dealloc(alloc, (void*)path);
    if (!a)
    {
        t->status = MEL_STORAGE_ERROR | MEL_STORAGE_NOT_FOUND;
        return;
    }
    off_t len = AAsset_getLength64(a);
    usize expect = mel_storage_job_read_expect(job);
    if (expect != 0 && (usize)len != expect)
    {
        AAsset_close(a);
        t->status = MEL_STORAGE_ERROR | MEL_STORAGE_SIZE_MISMATCH;
        return;
    }
    if (len > 0)
    {
        t->bytes = mel_alloc(alloc, (usize)len);
        if (!t->bytes)
        {
            AAsset_close(a);
            t->status = MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE;
            return;
        }
        memcpy(t->bytes, AAsset_getBuffer(a), (usize)len);
    }
    t->bytes_len = (usize)len;
    AAsset_close(a);
    t->status = MEL_STORAGE_OK;
}

static void work_size(Asset_Task* t)
{
    Mel_Storage_Job* job = t->job;
    const Mel_Alloc* alloc = mel_storage_job_alloc(job);
    const char*      path = job_cpath(job, mel_storage_job_path(job));
    AAsset*          a = path ? AAssetManager_open(g_assets, path, AASSET_MODE_UNKNOWN) : NULL;
    if (path)
        mel_dealloc(alloc, (void*)path);
    if (!a)
    {
        t->status = MEL_STORAGE_ERROR | MEL_STORAGE_NOT_FOUND;
        return;
    }
    t->size = (u64)AAsset_getLength64(a);
    AAsset_close(a);
    t->status = MEL_STORAGE_OK;
}

static void work_meta(Asset_Task* t)
{
    Mel_Storage_Job* job = t->job;
    const Mel_Alloc* alloc = mel_storage_job_alloc(job);
    const char*      path = job_cpath(job, mel_storage_job_path(job));
    t->meta.read_only = true;
    AAsset* a = path ? AAssetManager_open(g_assets, path, AASSET_MODE_UNKNOWN) : NULL;
    if (a)
    {
        t->meta.exists = true;
        t->meta.kind = MEL_STORAGE_KIND_FILE;
        t->meta.size_bytes = (u64)AAsset_getLength64(a);
        AAsset_close(a);
    }
    else if (asset_is_dir(path))
    {
        t->meta.exists = true;
        t->meta.kind = MEL_STORAGE_KIND_DIR;
    }
    if (path)
        mel_dealloc(alloc, (void*)path);
    t->status = MEL_STORAGE_OK;
}

static void work_list(Asset_Task* t, bool glob)
{
    Mel_Storage_Job* job = t->job;
    const Mel_Alloc* alloc = mel_storage_job_alloc(job);
    const char*      path = job_cpath(job, mel_storage_job_path(job));
    AAssetDir*       d = AAssetManager_openDir(g_assets, path ? path : "");
    if (path)
        mel_dealloc(alloc, (void*)path);
    if (!d)
    {
        t->status = MEL_STORAGE_ERROR | MEL_STORAGE_NOT_FOUND;
        return;
    }

    Mel_Array(Mel_Storage_Entry) acc;
    mel_array_init(&acc, alloc);
    str8 pattern = mel_storage_job_pattern(job);
    bool ci = mel_storage_job_case_insensitive(job);
    for (const char* name = AAssetDir_getNextFileName(d); name; name = AAssetDir_getNextFileName(d))
    {
        str8 nm = str8_from_cstr(name);
        if (glob && !mel_fs_glob_match(pattern, nm, ci))
            continue;
        Mel_Storage_Entry e = { 0 };
        e.name = str8_dup_alloc(nm, alloc);
        e.kind = MEL_STORAGE_KIND_FILE;
        mel_array_push(&acc, e);
    }
    AAssetDir_close(d);

    u32 count = (u32)acc.count;
    if (count > 0)
    {
        t->entries = mel_alloc_array(alloc, Mel_Storage_Entry, count);
        if (!t->entries)
        {
            for (u32 i = 0; i < count; i++)
                if (acc.items[i].name.data)
                    mel_dealloc(alloc, acc.items[i].name.data);
            mel_array_free(&acc);
            t->status = MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE;
            return;
        }
        memcpy(t->entries, acc.items, sizeof(Mel_Storage_Entry) * count);
    }
    t->entry_count = count;
    mel_array_free(&acc);
    t->status = MEL_STORAGE_OK;
}

static void work_run(Asset_Task* t)
{
    switch (mel_storage_job_kind(t->job))
    {
    case MEL_STORAGE_JOB_READ:
        work_read(t);
        break;
    case MEL_STORAGE_JOB_SIZE:
        work_size(t);
        break;
    case MEL_STORAGE_JOB_META:
        work_meta(t);
        break;
    case MEL_STORAGE_JOB_ENUMERATE:
        work_list(t, false);
        break;
    case MEL_STORAGE_JOB_GLOB:
        work_list(t, true);
        break;
    case MEL_STORAGE_JOB_WRITE:
    case MEL_STORAGE_JOB_MKDIR:
    case MEL_STORAGE_JOB_REMOVE:
    case MEL_STORAGE_JOB_RENAME:
    case MEL_STORAGE_JOB_COPY:
        t->status = MEL_STORAGE_ERROR | MEL_STORAGE_READ_ONLY;
        break;
    case MEL_STORAGE_JOB_SPACE:
        t->status = MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE;
        break;
    default:
        t->status = MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE;
        break;
    }
}

static void settle_from_task(Asset_Task* t)
{
    Mel_Storage_Job* job = t->job;
    switch (mel_storage_job_kind(job))
    {
    case MEL_STORAGE_JOB_READ:
        mel_storage_job_settle_bytes(job, t->bytes, t->bytes_len, t->status);
        break;
    case MEL_STORAGE_JOB_SIZE:
        mel_storage_job_settle_size(job, t->size, t->status);
        break;
    case MEL_STORAGE_JOB_META:
        mel_storage_job_settle_meta(job, t->meta, t->status);
        break;
    case MEL_STORAGE_JOB_ENUMERATE:
    case MEL_STORAGE_JOB_GLOB:
        mel_storage_job_settle_list(job, t->entries, t->entry_count, t->status);
        break;
    case MEL_STORAGE_JOB_SPACE:
    {
        Mel_Storage_Space sp = { 0 };
        mel_storage_job_settle_space(job, sp, t->status);
        break;
    }
    default:
        mel_storage_job_settle_void(job, t->status);
        break;
    }
}

static void completion_run(Mel_Task* self)
{
    Asset_Task*    t = mel_container_of(self, Asset_Task, completion_task);
    Asset_Backend* b = t->backend;
    if (t->job->cancel_requested)
    {
        const Mel_Alloc* alloc = mel_storage_job_alloc(t->job);
        if (t->bytes)
            mel_dealloc(alloc, t->bytes);
        if (t->entries)
        {
            for (u32 i = 0; i < t->entry_count; i++)
                if (t->entries[i].name.data)
                    mel_dealloc(alloc, t->entries[i].name.data);
            mel_dealloc(alloc, t->entries);
        }
        mel_storage_job_settle_void(t->job, MEL_STORAGE_ERROR | MEL_STORAGE_CANCELLED);
    }
    else
    {
        settle_from_task(t);
    }
    mel_vat_release(b->vat);
    mel_dealloc(b->alloc, t);
}

static int worker_main(void* user)
{
    Asset_Backend* b = (Asset_Backend*)user;
    for (;;)
    {
        mel_sem_wait(&b->items);
        if (atomic_load_explicit(&b->running, memory_order_acquire) == 0)
            break;
        Mel_Mpsc_Node* node = mel_mpsc_pop(&b->queue);
        if (!node)
            continue;
        Asset_Task* t = mel_container_of(node, Asset_Task, node);
        if (!t->job->cancel_requested)
            work_run(t);
        mel_vat_post(b->vat, &t->completion_task);
    }
    return 0;
}

static bool asset_ready(Mel_Storage* st, void* user)
{
    (void)st;
    (void)user;
    return g_assets != NULL;
}

static void asset_submit(Mel_Storage* st, void* user, Mel_Storage_Job* job)
{
    (void)st;
    Asset_Backend* b = (Asset_Backend*)user;
    if (job->cancel_requested)
    {
        mel_storage_job_settle_void(job, MEL_STORAGE_ERROR | MEL_STORAGE_CANCELLED);
        return;
    }
    Asset_Task* t = mel_alloc_type(b->alloc, Asset_Task);
    if (!t)
    {
        mel_storage_job_settle_void(job, MEL_STORAGE_ERROR | MEL_STORAGE_UNAVAILABLE);
        return;
    }
    memset(t, 0, sizeof *t);
    t->job = job;
    t->backend = b;
    if (atomic_load_explicit(&b->running, memory_order_acquire) == 0)
    {
        work_run(t);
        settle_from_task(t);
        mel_dealloc(b->alloc, t);
        return;
    }
    mel_task_init(&t->completion_task, completion_run);
    mel_vat_retain(b->vat);
    mel_mpsc_push(&b->queue, &t->node);
    mel_sem_post(&b->items);
}

static void asset_destroy(Mel_Storage* st, void* user)
{
    (void)st;
    Asset_Backend* b = (Asset_Backend*)user;
    if (!b)
        return;
    if (atomic_load_explicit(&b->running, memory_order_acquire) != 0)
    {
        atomic_store_explicit(&b->running, 0, memory_order_release);
        mel_sem_post(&b->items);
        mel_thread_join(&b->worker, NULL);
    }
    for (;;)
    {
        Mel_Mpsc_Node* node = mel_mpsc_pop(&b->queue);
        if (!node)
            break;
        Asset_Task* t = mel_container_of(node, Asset_Task, node);
        mel_storage_job_settle_void(t->job, MEL_STORAGE_ERROR | MEL_STORAGE_CANCELLED);
        mel_vat_release(b->vat);
        mel_dealloc(b->alloc, t);
    }
    mel_sem_destroy(&b->items);
    mel_dealloc(b->alloc, b);
}

static const Mel_Storage_Interface ASSET_IFACE = {
    .name = "android-assets",
    .ready = asset_ready,
    .submit = asset_submit,
    .cancel = NULL,
    .destroy = asset_destroy,
};

const Mel_Storage_Interface* mel_storage_title_interface(void) { return &ASSET_IFACE; }

Mel_Storage* mel_storage__open_title_native(Mel_Storage_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    if (!opt.vat)
    {
        mel_log_error("storage", "open_title: vat is required");
        return NULL;
    }
    if (!g_assets)
        return NULL;

    Asset_Backend* b = mel_alloc_type(alloc, Asset_Backend);
    if (!b)
        return NULL;
    memset(b, 0, sizeof *b);
    b->vat = opt.vat;
    b->alloc = alloc;
    mel_mpsc_init(&b->queue);
    if (!mel_sem_init(&b->items, 0))
    {
        mel_dealloc(alloc, b);
        return NULL;
    }
    atomic_store_explicit(&b->running, 1, memory_order_release);
    if (!mel_thread_spawn(&b->worker, worker_main, b, .name = "mel-storage-asset"))
    {
        atomic_store_explicit(&b->running, 0, memory_order_release);
        mel_log_warn("storage", "open_title: asset worker thread unavailable; reads run inline on the loop thread");
    }

    Mel_Storage* st = mel_storage_create_opt((Mel_Storage_Opt){ .vat = opt.vat, .alloc = alloc, .writable = false, .iface = &ASSET_IFACE, .backend_user = b });
    if (!st)
    {
        asset_destroy(NULL, b);
        return NULL;
    }
    return st;
}
