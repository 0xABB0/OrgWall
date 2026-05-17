#include "vfs.backend.os.h"
#include "vfs.h"
#include "str8.h"
#include "core.platform.h"

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

static char* mel__os_to_cstr(str8 path, char* buf, size_t cap)
{
    size_t len = (size_t)path.len < cap - 1 ? (size_t)path.len : cap - 1;
    memcpy(buf, path.data, len);
    buf[len] = 0;
    return buf;
}

static i32 mel__os_open(Mel_Vfs_Backend* self, str8 path, u32 flags)
{
    (void)self;
    char cpath[1024];
    mel__os_to_cstr(path, cpath, sizeof(cpath));

    int oflags = 0;
    bool read  = (flags & MEL_VFS_OPEN_READ) != 0;
    bool write = (flags & MEL_VFS_OPEN_WRITE) != 0;

    if (read && write)       oflags = O_RDWR;
    else if (write)          oflags = O_WRONLY;
    else                     oflags = O_RDONLY;

    if (flags & MEL_VFS_OPEN_CREATE)   oflags |= O_CREAT;
    if (flags & MEL_VFS_OPEN_TRUNCATE) oflags |= O_TRUNC;

    int fd = open(cpath, oflags, 0644);
    return (i32)fd;
}

static void mel__os_close(Mel_Vfs_Backend* self, i32 handle)
{
    (void)self;
    close(handle);
}

static i64 mel__os_read(Mel_Vfs_Backend* self, i32 handle, void* buf, i64 size, i64 offset)
{
    (void)self;
    ssize_t result = pread(handle, buf, (size_t)size, (off_t)offset);
    return (i64)result;
}

static i64 mel__os_write(Mel_Vfs_Backend* self, i32 handle, const void* buf, i64 size, i64 offset)
{
    (void)self;
    ssize_t result = pwrite(handle, buf, (size_t)size, (off_t)offset);
    return (i64)result;
}

static i64 mel__os_file_length(Mel_Vfs_Backend* self, i32 handle)
{
    (void)self;
    struct stat st;
    if (fstat(handle, &st) != 0) return -1;
    return (i64)st.st_size;
}

static bool mel__os_stat(Mel_Vfs_Backend* self, str8 path, Mel_Vfs_Stat* out)
{
    (void)self;
    char cpath[1024];
    mel__os_to_cstr(path, cpath, sizeof(cpath));

    struct stat st;
    if (stat(cpath, &st) != 0) return false;

    out->size = (u64)st.st_size;
    out->flags = 0;
    if (S_ISREG(st.st_mode)) out->flags |= MEL_VFS_STAT_IS_FILE;
    if (S_ISDIR(st.st_mode)) out->flags |= MEL_VFS_STAT_IS_DIR;
    return true;
}

static bool mel__os_enumerate(Mel_Vfs_Backend* self, str8 path, Mel_Vfs_Enumerate_Cb cb, void* user)
{
    (void)self;
    char cpath[1024];
    mel__os_to_cstr(path, cpath, sizeof(cpath));

    DIR* dir = opendir(cpath);
    if (!dir) return false;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == 0 ||
            (entry->d_name[1] == '.' && entry->d_name[2] == 0)))
            continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", cpath, entry->d_name);

        Mel_Vfs_Stat st = {0};
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0)
        {
            st.size = (u64)file_stat.st_size;
            if (S_ISREG(file_stat.st_mode)) st.flags |= MEL_VFS_STAT_IS_FILE;
            if (S_ISDIR(file_stat.st_mode)) st.flags |= MEL_VFS_STAT_IS_DIR;
        }

        str8 name = str8_from_cstr(entry->d_name);
        if (!cb(name, &st, user)) break;
    }

    closedir(dir);
    return true;
}

static bool mel__os_mkdir(Mel_Vfs_Backend* self, str8 path)
{
    (void)self;
    char cpath[1024];
    mel__os_to_cstr(path, cpath, sizeof(cpath));
    return mkdir(cpath, 0755) == 0 || errno == EEXIST;
}

static bool mel__os_delete(Mel_Vfs_Backend* self, str8 path)
{
    (void)self;
    char cpath[1024];
    mel__os_to_cstr(path, cpath, sizeof(cpath));
    return unlink(cpath) == 0 || rmdir(cpath) == 0;
}

static bool mel__os_exists(Mel_Vfs_Backend* self, str8 path)
{
    (void)self;
    char cpath[1024];
    mel__os_to_cstr(path, cpath, sizeof(cpath));
    return access(cpath, F_OK) == 0;
}

static void* mel__os_map(Mel_Vfs_Backend* self, i32 handle, i64 offset, i64 size, u32 prot)
{
    (void)self;
    int mprot = PROT_NONE;
    if (prot & MEL_VFS_MAP_READ)  mprot |= PROT_READ;
    if (prot & MEL_VFS_MAP_WRITE) mprot |= PROT_WRITE;

    void* ptr = mmap(NULL, (size_t)size, mprot, MAP_PRIVATE, handle, (off_t)offset);
    if (ptr == MAP_FAILED) return NULL;
    return ptr;
}

static void mel__os_unmap(Mel_Vfs_Backend* self, void* ptr, i64 size)
{
    (void)self;
    munmap(ptr, (size_t)size);
}

static Mel_Vfs_Backend s_os_backend = {
    .caps = MEL_VFS_CAP_READ | MEL_VFS_CAP_WRITE | MEL_VFS_CAP_MMAP | MEL_VFS_CAP_ASYNC | MEL_VFS_CAP_SEEK,
    .open = mel__os_open,
    .close = mel__os_close,
    .read = mel__os_read,
    .write = mel__os_write,
    .file_length = mel__os_file_length,
    .stat = mel__os_stat,
    .enumerate = mel__os_enumerate,
    .mkdir = mel__os_mkdir,
    .delete_fn = mel__os_delete,
    .exists = mel__os_exists,
    .map = mel__os_map,
    .unmap = mel__os_unmap,
};

Mel_Vfs_Backend* mel_vfs_backend_os(void)
{
    return &s_os_backend;
}
