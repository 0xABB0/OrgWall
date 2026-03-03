#include "vfs.backend.os.h"
#include "vfs.h"
#include "async.io.h"
#include "vfs.async.h"
#include "allocator.h"
#include "string.str8.h"
#include "core.platform.h"

#include <string.h>

#include "collection.array.h"

#if MEL_PLATFORM_POSIX

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <errno.h>

#if MEL_PLATFORM_APPLE
#include <sys/uio.h>
#include <sys/event.h>
#else
#include <sys/uio.h>
#endif

typedef struct {
    int kq_fd;
    int watch_fd;
    char watched_path[4096];
    usize watched_path_len;
    i32 pending_action;
    bool has_pending_event;
    bool active;
} Mel_Vfs__Os_Watch;

typedef struct {
    DIR* dp;
    u8   pending_name[MEL_VFS_MAX_PATH];
    usize pending_name_len;
    Mel_Vfs_Stat pending_stat;
    bool has_pending;
} Mel_Vfs__Os_Open_Dir;

typedef struct {
    const Mel_Alloc* alloc;
    char root[4096];
    usize root_len;
#if MEL_PLATFORM_APPLE
    Mel_Array(Mel_Vfs__Os_Watch) watches;
#endif
} Mel_Vfs__Os_Data;

static void mel__vfs_os_make_full_path(Mel_Vfs__Os_Data* d, str8 rel, char* out, usize out_cap)
{
    usize total = d->root_len + 1 + (usize)rel.len + 1;
    assert(total <= out_cap);

    memcpy(out, d->root, d->root_len);
    usize w = d->root_len;

    if (rel.len > 0 && !(rel.len == 1 && rel.data[0] == '.')) {
        out[w++] = '/';
        memcpy(out + w, rel.data, (usize)rel.len);
        w += (usize)rel.len;
    }

    out[w] = '\0';
}

static i32 mel__vfs_os_open(Mel_Vfs_Backend* b, str8 path, u32 flags, Mel_Vfs_Native_Handle* out)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    char full[4096];
    mel__vfs_os_make_full_path(d, path, full, sizeof(full));

    int oflags = 0;
    if ((flags & MEL_VFS_OPEN_READ) && (flags & MEL_VFS_OPEN_WRITE))
        oflags = O_RDWR;
    else if (flags & MEL_VFS_OPEN_WRITE)
        oflags = O_WRONLY;
    else
        oflags = O_RDONLY;

    if (flags & MEL_VFS_OPEN_CREATE)   oflags |= O_CREAT;
    if (flags & MEL_VFS_OPEN_TRUNCATE) oflags |= O_TRUNC;

#ifdef O_DIRECT
    if (flags & MEL_VFS_OPEN_DIRECT)   oflags |= O_DIRECT;
#endif

    int fd = open(full, oflags, 0644);
    if (fd < 0) return -errno;

#ifdef F_NOCACHE
    if (flags & MEL_VFS_OPEN_DIRECT) {
        fcntl(fd, F_NOCACHE, 1);
    }
#endif

    *out = (Mel_Vfs_Native_Handle)fd;
    return 0;
}

static void mel__vfs_os_close(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    MEL_UNUSED(b);
    close((int)h);
}

static i64 mel__vfs_os_read(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, void* dst, usize size)
{
    MEL_UNUSED(b);
    ssize_t n = pread((int)h, dst, size, (off_t)offset);
    return (n < 0) ? -errno : (i64)n;
}

static i64 mel__vfs_os_write(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, const void* src, usize size)
{
    MEL_UNUSED(b);
    ssize_t n = pwrite((int)h, src, size, (off_t)offset);
    return (n < 0) ? -errno : (i64)n;
}

static i64 mel__vfs_os_readv(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, Mel_IoVec* iov, usize iov_cnt)
{
    MEL_UNUSED(b);
    ssize_t n = preadv((int)h, (struct iovec*)iov, (int)iov_cnt, (off_t)offset);
    return (n < 0) ? -errno : (i64)n;
}

static i64 mel__vfs_os_writev(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, const Mel_IoVec* iov, usize iov_cnt)
{
    MEL_UNUSED(b);
    ssize_t n = pwritev((int)h, (const struct iovec*)iov, (int)iov_cnt, (off_t)offset);
    return (n < 0) ? -errno : (i64)n;
}

static void mel__vfs_os_stat_from_posix(struct stat* st, Mel_Vfs_Stat* out)
{
    out->size = (u64)st->st_size;

#if MEL_PLATFORM_APPLE
    out->mtime_ns = (u64)st->st_mtimespec.tv_sec * 1000000000ULL + (u64)st->st_mtimespec.tv_nsec;
    out->change_ns = (u64)st->st_ctimespec.tv_sec * 1000000000ULL + (u64)st->st_ctimespec.tv_nsec;
    out->birth_ns = (u64)st->st_birthtimespec.tv_sec * 1000000000ULL + (u64)st->st_birthtimespec.tv_nsec;
    out->flags = MEL_VFS_STAT_HAS_BIRTH_TIME;
#else
    out->mtime_ns = (u64)st->st_mtim.tv_sec * 1000000000ULL + (u64)st->st_mtim.tv_nsec;
    out->change_ns = (u64)st->st_ctim.tv_sec * 1000000000ULL + (u64)st->st_ctim.tv_nsec;
    out->birth_ns = 0;
    out->flags = 0;
#endif

    if (S_ISREG(st->st_mode))  out->flags |= MEL_VFS_STAT_IS_FILE;
    if (S_ISDIR(st->st_mode))  out->flags |= MEL_VFS_STAT_IS_DIR;
    if (S_ISLNK(st->st_mode))  out->flags |= MEL_VFS_STAT_IS_SYMLINK;
    if (!(st->st_mode & S_IWUSR)) out->flags |= MEL_VFS_STAT_IS_READONLY;
}

static i32 mel__vfs_os_stat(Mel_Vfs_Backend* b, str8 path, Mel_Vfs_Stat* out)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    char full[4096];
    mel__vfs_os_make_full_path(d, path, full, sizeof(full));

    struct stat st;
    if (stat(full, &st) != 0) return -errno;

    mel__vfs_os_stat_from_posix(&st, out);
    return 0;
}

static i32 mel__vfs_os_dir_open(Mel_Vfs_Backend* b, str8 path, Mel_Vfs_Native_Handle* out)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    char full[4096];
    mel__vfs_os_make_full_path(d, path, full, sizeof(full));

    DIR* dp = opendir(full);
    if (!dp) return -errno;

    Mel_Vfs__Os_Open_Dir* od = mel_calloc(d->alloc, sizeof(Mel_Vfs__Os_Open_Dir));
    od->dp = dp;

    *out = (Mel_Vfs_Native_Handle)od;
    return 0;
}

static i32 mel__vfs_os_dir_next(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h,
                                 u8* name_buf, usize name_cap, usize* out_name_len,
                                 Mel_Vfs_Stat* out_stat)
{
    Mel_Vfs__Os_Open_Dir* od = (Mel_Vfs__Os_Open_Dir*)h;
    assert(od);
    DIR* dp = od->dp;

    if (od->has_pending) {
        *out_name_len = od->pending_name_len;
        if (od->pending_name_len > name_cap) return -1;

        memcpy(name_buf, od->pending_name, od->pending_name_len);
        *out_stat = od->pending_stat;
        od->has_pending = false;
        return 0;
    }

    for (;;) {
        errno = 0;
        struct dirent* ent = readdir(dp);
        if (!ent) {
            if (errno != 0) return -errno;
            return 1;
        }

        if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
            (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        usize len = strlen(ent->d_name);
        Mel_Vfs_Stat entry_stat = {0};
#ifdef _DIRENT_HAVE_D_TYPE
        if (ent->d_type == DT_REG) entry_stat.flags = MEL_VFS_STAT_IS_FILE;
        else if (ent->d_type == DT_DIR) entry_stat.flags = MEL_VFS_STAT_IS_DIR;
        else if (ent->d_type == DT_LNK) entry_stat.flags = MEL_VFS_STAT_IS_SYMLINK;
#endif

        *out_name_len = len;
        if (len > name_cap) {
            assert(len <= sizeof(od->pending_name));
            if (len > sizeof(od->pending_name)) return -ENAMETOOLONG;
            memcpy(od->pending_name, ent->d_name, len);
            od->pending_name_len = len;
            od->pending_stat = entry_stat;
            od->has_pending = true;
            return -1;
        }

        memcpy(name_buf, ent->d_name, len);
        *out_stat = entry_stat;
        return 0;
    }
}

static void mel__vfs_os_dir_close(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    Mel_Vfs__Os_Open_Dir* od = (Mel_Vfs__Os_Open_Dir*)h;
    if (!od) return;
    if (od->dp) closedir(od->dp);
    mel_dealloc(d->alloc, od);
}

static i32 mel__vfs_os_map(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, usize size, u32 flags, void** out_ptr)
{
    MEL_UNUSED(b);
    int prot = 0;
    if (flags & MEL_VFS_MAP_READ)  prot |= PROT_READ;
    if (flags & MEL_VFS_MAP_WRITE) prot |= PROT_WRITE;

    void* ptr = mmap(NULL, size, prot, MAP_PRIVATE, (int)h, (off_t)offset);
    if (ptr == MAP_FAILED) return -errno;

    *out_ptr = ptr;
    return 0;
}

static void mel__vfs_os_unmap(Mel_Vfs_Backend* b, void* ptr, usize size)
{
    MEL_UNUSED(b);
    munmap(ptr, size);
}

static i32 mel__vfs_os_sync(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    MEL_UNUSED(b);
    if (fsync((int)h) != 0) return -errno;
    return 0;
}

static i32 mel__vfs_os_rename(Mel_Vfs_Backend* b, str8 old_path, str8 new_path)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    char full_old[4096], full_new[4096];
    mel__vfs_os_make_full_path(d, old_path, full_old, sizeof(full_old));
    mel__vfs_os_make_full_path(d, new_path, full_new, sizeof(full_new));
    if (rename(full_old, full_new) != 0) return -errno;
    return 0;
}

static i32 mel__vfs_os_remove(Mel_Vfs_Backend* b, str8 path)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    char full[4096];
    mel__vfs_os_make_full_path(d, path, full, sizeof(full));
    if (remove(full) != 0) return -errno;
    return 0;
}

static i32 mel__vfs_os_mkdir(Mel_Vfs_Backend* b, str8 path)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    char full[4096];
    mel__vfs_os_make_full_path(d, path, full, sizeof(full));
    if (mkdir(full, 0755) != 0) return -errno;
    return 0;
}

#if MEL_PLATFORM_APPLE

static i32 mel__vfs_os_watch_open(Mel_Vfs_Backend* b, str8 path, bool recursive, u32 flags, Mel_Vfs_Native_Handle* out)
{
    MEL_UNUSED(flags);

    if (recursive) return -(i32)ENOTSUP;

    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    char full[4096];
    mel__vfs_os_make_full_path(d, path, full, sizeof(full));

    int fd = open(full, O_EVTONLY);
    if (fd < 0) return -errno;

    int kq = kqueue();
    if (kq < 0)
    {
        int saved = errno;
        close(fd);
        return -saved;
    }

    struct kevent ev;
    EV_SET(&ev, (uintptr_t)fd, EVFILT_VNODE,
           EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB,
           0, NULL);

    if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0)
    {
        int saved = errno;
        close(kq);
        close(fd);
        return -saved;
    }

    Mel_Vfs__Os_Watch watch = {
        .kq_fd = kq,
        .watch_fd = fd,
        .watched_path_len = strlen(full),
        .active = true,
    };
    usize path_len = watch.watched_path_len;
    if (path_len >= sizeof(watch.watched_path))
        path_len = sizeof(watch.watched_path) - 1;
    memcpy(watch.watched_path, full, path_len);
    watch.watched_path[path_len] = '\0';
    watch.watched_path_len = path_len;

    usize slot = d->watches.count;
    for (usize i = 0; i < d->watches.count; i++)
    {
        if (!d->watches.items[i].active)
        {
            slot = i;
            break;
        }
    }

    if (slot == d->watches.count)
        mel_array_push(&d->watches, watch);
    else
        d->watches.items[slot] = watch;

    *out = (Mel_Vfs_Native_Handle)(slot + 1);
    return 0;
}

static i32 mel__vfs_os_watch_next(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h,
                                   i32 timeout_ms, u8* path_buf, usize path_cap,
                                   usize* out_path_len, i32* out_action)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    usize idx = (usize)h - 1;
    assert(idx < d->watches.count);
    Mel_Vfs__Os_Watch* w = &d->watches.items[idx];
    assert(w->active);

    if (w->has_pending_event) {
        *out_path_len = w->watched_path_len;
        if (w->watched_path_len > path_cap) return -1;
        memcpy(path_buf, w->watched_path, w->watched_path_len);
        *out_action = w->pending_action;
        w->has_pending_event = false;
        return 0;
    }

    struct timespec ts_val;
    struct timespec* ts = NULL;
    if (timeout_ms >= 0)
    {
        ts_val.tv_sec = timeout_ms / 1000;
        ts_val.tv_nsec = (timeout_ms % 1000) * 1000000L;
        ts = &ts_val;
    }

    struct kevent ev;
    int n = kevent(w->kq_fd, NULL, 0, &ev, 1, ts);
    if (n < 0) return -errno;

    if (n == 0) return 1;

    i32 action = MEL_VFS_WATCH_MODIFIED;
    if (ev.fflags & NOTE_DELETE)
        action = MEL_VFS_WATCH_REMOVED;
    else if (ev.fflags & NOTE_RENAME)
        action = MEL_VFS_WATCH_RENAMED;
    else if (ev.fflags & NOTE_WRITE)
        action = MEL_VFS_WATCH_MODIFIED;
    else if (ev.fflags & NOTE_ATTRIB)
        action = MEL_VFS_WATCH_MODIFIED;

    *out_path_len = w->watched_path_len;
    if (w->watched_path_len > path_cap) {
        w->pending_action = action;
        w->has_pending_event = true;
        return -1;
    }

    memcpy(path_buf, w->watched_path, w->watched_path_len);
    *out_action = action;

    return 0;
}

static void mel__vfs_os_watch_close(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    usize idx = (usize)h - 1;
    assert(idx < d->watches.count);
    Mel_Vfs__Os_Watch* w = &d->watches.items[idx];
    assert(w->active);

    close(w->kq_fd);
    close(w->watch_fd);
    w->has_pending_event = false;
    w->active = false;
}

#endif

#if MEL_PLATFORM_APPLE
#include <dispatch/dispatch.h>

static bool mel__vfs_os_try_submit_native(Mel_Vfs_Backend* b, struct Mel_Vfs_Sqe* sqe, Mel_Vfs_Native_Handle h, Mel_Vfs* vfs, void* op)
{
    if (sqe->op == MEL_VFS_OP_READ) {
        int fd = (int)h;
        dispatch_io_t channel = dispatch_io_create(DISPATCH_IO_RANDOM, fd, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(int error) {
        });
        
        Mel_Vfs__Op* vop = (Mel_Vfs__Op*)op;
        __block size_t accumulated = 0;
        __block int final_error = 0;

        dispatch_io_read(channel, (off_t)sqe->read.offset, (size_t)sqe->read.size, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(bool done, dispatch_data_t data, int error) {
            if (error != 0) final_error = error;
            
            if (data) {
                dispatch_data_apply(data, ^bool(dispatch_data_t region, size_t offset, const void *buffer, size_t size) {
                    memcpy((u8*)sqe->read.dst + accumulated + offset, buffer, size);
                    return true;
                });
                accumulated += dispatch_data_get_size(data);
            }

            if (done) {
                vop->cqe.status = (final_error == 0) ? MEL_VFS_STATUS_OK : MEL_VFS_STATUS_IO_ERROR;
                vop->cqe.result = (final_error == 0) ? (i64)accumulated : -final_error;
                
                Mel_Io_Cqe io_cqe = {
                    .ticket = sqe->ticket,
                    .handler_id = vfs->handler_id,
                    .status = (vop->cqe.status == MEL_VFS_STATUS_OK) ? MEL_IO_STATUS_OK : MEL_IO_STATUS_ERROR,
                    .result = vop->cqe.result,
                    .user_data = sqe->user_data,
                    .result_data = op,
                };
                mel_io_post_cqe(vfs->io, io_cqe);
                dispatch_release(channel);
            }
        });
        return true;
    }
    return false;
}
#endif

static void mel__vfs_os_destroy(Mel_Vfs_Backend* b)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    const Mel_Alloc* alloc = d->alloc;

#if MEL_PLATFORM_APPLE
    for (usize i = 0; i < d->watches.count; i++)
    {
        if (d->watches.items[i].active)
        {
            close(d->watches.items[i].kq_fd);
            close(d->watches.items[i].watch_fd);
        }
    }
    mel_array_free(&d->watches);
#endif

    mel_dealloc(alloc, d);
    mel_dealloc(alloc, b);
}

Mel_Vfs_Backend* mel_vfs_backend_os_create(const Mel_Alloc* alloc, str8 root_path)
{
    assert(alloc);

    Mel_Vfs_Backend* b = mel_calloc(alloc, sizeof(Mel_Vfs_Backend));
    Mel_Vfs__Os_Data* d = mel_calloc(alloc, sizeof(Mel_Vfs__Os_Data));

    d->alloc = alloc;
    usize len = (usize)root_path.len;
    assert(len < sizeof(d->root));
    memcpy(d->root, root_path.data, len);
    while (len > 1 && d->root[len - 1] == '/') len--;
    d->root[len] = '\0';
    d->root_len = len;

    b->open       = mel__vfs_os_open;
    b->close      = mel__vfs_os_close;
    b->read       = mel__vfs_os_read;
    b->write      = mel__vfs_os_write;
    b->readv      = mel__vfs_os_readv;
    b->writev     = mel__vfs_os_writev;
    b->stat       = mel__vfs_os_stat;
    b->dir_open   = mel__vfs_os_dir_open;
    b->dir_next   = mel__vfs_os_dir_next;
    b->dir_close  = mel__vfs_os_dir_close;
    b->map        = mel__vfs_os_map;
    b->unmap      = mel__vfs_os_unmap;
#if MEL_PLATFORM_APPLE
    mel_array_init(&d->watches, alloc);
    b->watch_open  = mel__vfs_os_watch_open;
    b->watch_next  = mel__vfs_os_watch_next;
    b->watch_close = mel__vfs_os_watch_close;
#else
    b->watch_open  = NULL;
    b->watch_next  = NULL;
    b->watch_close = NULL;
#endif
    b->sync       = mel__vfs_os_sync;
    b->rename     = mel__vfs_os_rename;
    b->remove     = mel__vfs_os_remove;
    b->mkdir      = mel__vfs_os_mkdir;
    b->destroy    = mel__vfs_os_destroy;
    b->impl_data  = d;

#if MEL_PLATFORM_APPLE
    b->try_submit_native = mel__vfs_os_try_submit_native;
#endif

    return b;
}

void mel_vfs_backend_os_destroy(Mel_Vfs_Backend* backend)
{
    assert(backend);
    backend->destroy(backend);
}

#endif
