#include "vfs.backend.os.h"
#include "vfs.async.h"
#include "allocator.h"
#include "string.str8.h"
#include "core.platform.h"

#include <string.h>

#if MEL_PLATFORM_POSIX

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <errno.h>

#if MEL_PLATFORM_APPLE
#include <sys/uio.h>
#else
#include <sys/uio.h>
#endif

typedef struct {
    const Mel_Alloc* alloc;
    char root[4096];
    usize root_len;
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

    int fd = open(full, oflags, 0644);
    if (fd < 0) return -errno;

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

    *out = (Mel_Vfs_Native_Handle)dp;
    return 0;
}

static i32 mel__vfs_os_dir_next(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h,
                                 u8* name_buf, usize name_cap, usize* out_name_len,
                                 Mel_Vfs_Stat* out_stat)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    MEL_UNUSED(d);
    DIR* dp = (DIR*)h;

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
        *out_name_len = len;
        if (len > name_cap) return -1;

        memcpy(name_buf, ent->d_name, len);

        memset(out_stat, 0, sizeof(*out_stat));
#ifdef _DIRENT_HAVE_D_TYPE
        if (ent->d_type == DT_REG) out_stat->flags = MEL_VFS_STAT_IS_FILE;
        else if (ent->d_type == DT_DIR) out_stat->flags = MEL_VFS_STAT_IS_DIR;
        else if (ent->d_type == DT_LNK) out_stat->flags = MEL_VFS_STAT_IS_SYMLINK;
#endif
        return 0;
    }
}

static void mel__vfs_os_dir_close(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    MEL_UNUSED(b);
    closedir((DIR*)h);
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

static void mel__vfs_os_destroy(Mel_Vfs_Backend* b)
{
    Mel_Vfs__Os_Data* d = (Mel_Vfs__Os_Data*)b->impl_data;
    const Mel_Alloc* alloc = d->alloc;
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
    b->watch_open  = NULL;
    b->watch_next  = NULL;
    b->watch_close = NULL;
    b->sync       = mel__vfs_os_sync;
    b->destroy    = mel__vfs_os_destroy;
    b->impl_data  = d;

    return b;
}

void mel_vfs_backend_os_destroy(Mel_Vfs_Backend* backend)
{
    assert(backend);
    backend->destroy(backend);
}

#endif
