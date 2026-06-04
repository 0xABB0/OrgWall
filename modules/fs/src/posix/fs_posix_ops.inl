#include "../fs_internal.h"

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <string/path.h>
#include <log/log.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef Mel_Array(Mel_Fs_Dir_Entry) Fs_Entry_Array;

bool mel_fs__backend_available(void) { return true; }

static i64 posix_timespec_ns(struct timespec ts) { return (i64)ts.tv_sec * 1000000000ll + (i64)ts.tv_nsec; }

static Mel_Fs_Kind posix_kind_from_mode(mode_t m)
{
    if (S_ISREG(m))
        return MEL_FS_KIND_FILE;
    if (S_ISDIR(m))
        return MEL_FS_KIND_DIR;
    if (S_ISLNK(m))
        return MEL_FS_KIND_SYMLINK;
    return MEL_FS_KIND_OTHER;
}

static void posix_fill_stat(Mel_Fs_Stat* out, const struct stat* st)
{
    out->exists = true;
    out->kind = posix_kind_from_mode(st->st_mode);
    out->size_bytes = (u64)st->st_size;
    out->mode_bits = (u32)(st->st_mode & 07777);
    out->device_id = (u64)st->st_dev;
    out->inode = (u64)st->st_ino;
#if defined(__APPLE__)
    out->ctime_ns = posix_timespec_ns(st->st_ctimespec);
    out->mtime_ns = posix_timespec_ns(st->st_mtimespec);
    out->atime_ns = posix_timespec_ns(st->st_atimespec);
#else
    out->ctime_ns = posix_timespec_ns(st->st_ctim);
    out->mtime_ns = posix_timespec_ns(st->st_mtim);
    out->atime_ns = posix_timespec_ns(st->st_atim);
#endif
}

void mel_fs__do_stat(Mel_Fs_Op_Record* op)
{
    const char* path = str8_to_cstr(op->path_a, op->alloc);
    struct stat st;
    int         rc = op->follow_symlinks ? stat(path, &st) : lstat(path, &st);
    if (rc != 0)
    {
        int e = errno;
        mel_dealloc(op->alloc, (void*)path);
        if (e == ENOENT)
        {
            op->result.stat.value.exists = false;
            op->result.stat.status = MEL_FS_OK;
            op->result.stat.os_error = 0;
            return;
        }
        op->result.stat.status = mel_fs__status_from_errno(e);
        op->result.stat.os_error = e;
        return;
    }
    posix_fill_stat(&op->result.stat.value, &st);
    op->result.stat.value.read_only = access(path, W_OK) != 0;
    op->result.stat.status = MEL_FS_OK;
    op->result.stat.os_error = 0;
    mel_dealloc(op->alloc, (void*)path);
}

void mel_fs__do_exists(Mel_Fs_Op_Record* op)
{
    const char* path = str8_to_cstr(op->path_a, op->alloc);
    struct stat st;
    int         rc = lstat(path, &st);
    mel_dealloc(op->alloc, (void*)path);
    if (rc == 0)
    {
        op->result.boolean.existed = true;
        op->result.boolean.status = MEL_FS_OK;
        return;
    }
    int e = errno;
    if (e == ENOENT)
    {
        op->result.boolean.existed = false;
        op->result.boolean.status = MEL_FS_OK;
        return;
    }
    op->result.boolean.existed = false;
    op->result.boolean.status = mel_fs__status_from_errno(e);
    op->result.boolean.os_error = e;
}

static bool posix_mkdir_parents(const char* path, mode_t mode, int* out_err)
{
    usize len = strlen(path);
    char  buf[4096];
    if (len >= sizeof buf)
    {
        *out_err = ENAMETOOLONG;
        return false;
    }
    memcpy(buf, path, len + 1);
    for (usize i = 1; i < len; i++)
    {
        if (buf[i] == '/')
        {
            buf[i] = '\0';
            if (mkdir(buf, mode) != 0 && errno != EEXIST)
            {
                *out_err = errno;
                return false;
            }
            buf[i] = '/';
        }
    }
    if (mkdir(buf, mode) != 0 && errno != EEXIST)
    {
        *out_err = errno;
        return false;
    }
    *out_err = 0;
    return true;
}

void mel_fs__do_mkdir(Mel_Fs_Op_Record* op)
{
    const char* path = str8_to_cstr(op->path_a, op->alloc);
    int         e = 0;
    bool        ok;
    if (op->parents)
        ok = posix_mkdir_parents(path, (mode_t)op->mode_bits, &e);
    else
        ok = mkdir(path, (mode_t)op->mode_bits) == 0 || (e = errno) == EEXIST;
    mel_dealloc(op->alloc, (void*)path);
    if (ok || e == EEXIST)
    {
        op->result.voided.status = MEL_FS_OK;
        op->result.voided.os_error = 0;
        return;
    }
    op->result.voided.status = mel_fs__status_from_errno(e);
    op->result.voided.os_error = e;
}

static int posix_remove_recursive(const char* path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno;
    if (S_ISDIR(st.st_mode))
    {
        DIR* d = opendir(path);
        if (!d)
            return errno;
        struct dirent* ent;
        int            err = 0;
        while ((ent = readdir(d)) != NULL)
        {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            char child[4096];
            int  n = snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
            if (n < 0 || (usize)n >= sizeof child)
            {
                err = ENAMETOOLONG;
                break;
            }
            err = posix_remove_recursive(child);
            if (err != 0)
                break;
        }
        closedir(d);
        if (err != 0)
            return err;
        if (rmdir(path) != 0)
            return errno;
        return 0;
    }
    if (unlink(path) != 0)
        return errno;
    return 0;
}

void mel_fs__do_remove(Mel_Fs_Op_Record* op)
{
    const char* path = str8_to_cstr(op->path_a, op->alloc);
    int         e = 0;
    if (op->recursive)
    {
        e = posix_remove_recursive(path);
    }
    else
    {
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
            e = rmdir(path) == 0 ? 0 : errno;
        else
            e = unlink(path) == 0 ? 0 : errno;
    }
    mel_dealloc(op->alloc, (void*)path);
    op->result.voided.status = mel_fs__status_from_errno(e);
    op->result.voided.os_error = e;
}

void mel_fs__do_rename(Mel_Fs_Op_Record* op)
{
    const char* from = str8_to_cstr(op->path_a, op->alloc);
    const char* to = str8_to_cstr(op->path_b, op->alloc);
    int         e = 0;
    if (!op->overwrite)
    {
        struct stat st;
        if (lstat(to, &st) == 0)
            e = EEXIST;
    }
    if (e == 0 && rename(from, to) != 0)
        e = errno;
    mel_dealloc(op->alloc, (void*)from);
    mel_dealloc(op->alloc, (void*)to);
    op->result.voided.status = mel_fs__status_from_errno(e);
    op->result.voided.os_error = e;
}

static int posix_copy_bytes(const char* from, const char* to, mode_t mode, int extra_flags)
{
    int in = open(from, O_RDONLY);
    if (in < 0)
        return errno;
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC | extra_flags, mode);
    if (out < 0)
    {
        int e = errno;
        close(in);
        return e;
    }
    char    buf[1 << 16];
    ssize_t n;
    int     e = 0;
    while ((n = read(in, buf, sizeof buf)) > 0)
    {
        ssize_t off = 0;
        while (off < n)
        {
            ssize_t w = write(out, buf + off, (usize)(n - off));
            if (w < 0)
            {
                e = errno;
                goto done;
            }
            off += w;
        }
    }
    if (n < 0)
        e = errno;
done:
    close(in);
    if (close(out) != 0 && e == 0)
        e = errno;
    return e;
}

void mel_fs__do_copy(Mel_Fs_Op_Record* op)
{
    const char* from = str8_to_cstr(op->path_a, op->alloc);
    const char* to = str8_to_cstr(op->path_b, op->alloc);
    int         e = 0;

    struct stat sst;
    if (stat(from, &sst) != 0)
    {
        e = errno;
        goto finish;
    }
    if (!op->overwrite)
    {
        struct stat dst;
        if (lstat(to, &dst) == 0)
        {
            e = EEXIST;
            goto finish;
        }
    }
    mode_t mode = (mode_t)(sst.st_mode & 07777);
    if (op->atomic)
    {
        char tmp[4096];
        int  n = snprintf(tmp, sizeof tmp, "%s.mel-tmp-%d", to, (int)getpid());
        if (n < 0 || (usize)n >= sizeof tmp)
        {
            e = ENAMETOOLONG;
            goto finish;
        }
        e = posix_copy_bytes(from, tmp, mode, O_EXCL);
        if (e == 0 && rename(tmp, to) != 0)
        {
            e = errno;
            unlink(tmp);
        }
    }
    else
    {
        e = posix_copy_bytes(from, to, mode, 0);
    }

finish:
    mel_dealloc(op->alloc, (void*)from);
    mel_dealloc(op->alloc, (void*)to);
    op->result.voided.status = mel_fs__status_from_errno(e);
    op->result.voided.os_error = e;
}

void mel_fs__do_read_file(Mel_Fs_Op_Record* op)
{
    const char* path = str8_to_cstr(op->path_a, op->alloc);
    int         fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        int e = errno;
        mel_dealloc(op->alloc, (void*)path);
        op->result.bytes.status = mel_fs__status_from_errno(e);
        op->result.bytes.os_error = e;
        return;
    }
    struct stat st;
    usize       cap = 0;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
        cap = (usize)st.st_size;

    u8*   buf = cap > 0 ? mel_alloc(op->alloc, cap) : NULL;
    usize len = 0;
    int   e = 0;
    if (cap > 0 && !buf)
    {
        e = ENOMEM;
    }
    else
    {
        for (;;)
        {
            if (len == cap)
            {
                usize ncap = cap == 0 ? (1 << 16) : cap * 2;
                u8*   nbuf = buf ? mel_realloc(op->alloc, buf, ncap) : mel_alloc(op->alloc, ncap);
                if (!nbuf)
                {
                    e = ENOMEM;
                    break;
                }
                buf = nbuf;
                cap = ncap;
            }
            ssize_t n = read(fd, buf + len, cap - len);
            if (n < 0)
            {
                e = errno;
                break;
            }
            if (n == 0)
                break;
            len += (usize)n;
        }
    }
    close(fd);
    mel_dealloc(op->alloc, (void*)path);
    if (e != 0)
    {
        if (buf)
            mel_dealloc(op->alloc, buf);
        op->result.bytes.status = mel_fs__status_from_errno(e);
        op->result.bytes.os_error = e;
        return;
    }
    op->result.bytes.data = buf;
    op->result.bytes.len = len;
    op->result.bytes.status = MEL_FS_OK;
    op->result.bytes.os_error = 0;
}

void mel_fs__do_write_file(Mel_Fs_Op_Record* op)
{
    const char* path = str8_to_cstr(op->path_a, op->alloc);
    int         e = 0;

    if (op->create_parents)
    {
        str8 parent = mel_path_parent(op->path_a);
        if (parent.len > 0)
        {
            const char* pcstr = str8_to_cstr(parent, op->alloc);
            int         perr = 0;
            posix_mkdir_parents(pcstr, 0777, &perr);
            mel_dealloc(op->alloc, (void*)pcstr);
        }
    }

    char        tmp[4096];
    const char* target = path;
    if (op->atomic)
    {
        int n = snprintf(tmp, sizeof tmp, "%s.mel-tmp-%d", path, (int)getpid());
        if (n < 0 || (usize)n >= sizeof tmp)
        {
            e = ENAMETOOLONG;
            goto finish;
        }
        target = tmp;
    }

    int fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)op->mode_bits);
    if (fd < 0)
    {
        e = errno;
        goto finish;
    }
    usize off = 0;
    while (off < op->write_len)
    {
        ssize_t w = write(fd, op->write_data + off, op->write_len - off);
        if (w < 0)
        {
            e = errno;
            break;
        }
        off += (usize)w;
    }
    if (e == 0)
        fsync(fd);
    if (close(fd) != 0 && e == 0)
        e = errno;

    if (op->atomic)
    {
        if (e == 0 && rename(tmp, path) != 0)
            e = errno;
        if (e != 0)
            unlink(tmp);
    }

finish:
    mel_dealloc(op->alloc, (void*)path);
    op->result.voided.status = mel_fs__status_from_errno(e);
    op->result.voided.os_error = e;
}

#ifdef DT_DIR
static Mel_Fs_Kind posix_dirent_kind(unsigned char dt)
{
    switch (dt)
    {
    case DT_REG:
        return MEL_FS_KIND_FILE;
    case DT_DIR:
        return MEL_FS_KIND_DIR;
    case DT_LNK:
        return MEL_FS_KIND_SYMLINK;
    case DT_UNKNOWN:
        return MEL_FS_KIND_NONE;
    default:
        return MEL_FS_KIND_OTHER;
    }
}
#endif

static Mel_Fs_Kind posix_entry_kind(const struct dirent* ent)
{
#ifdef DT_DIR
    return posix_dirent_kind(ent->d_type);
#else
    (void)ent;
    return MEL_FS_KIND_NONE;
#endif
}

static bool posix_entry_is_dir(const struct dirent* ent, const char* full)
{
#ifdef DT_DIR
    if (ent->d_type == DT_DIR)
        return true;
    if (ent->d_type != DT_UNKNOWN)
        return false;
#else
    (void)ent;
#endif
    struct stat st;
    return stat(full, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool dir_push_entry(Mel_Fs_Op_Record* op, Fs_Entry_Array* arr, const char* dir, struct dirent* ent)
{
    Mel_Fs_Dir_Entry e = { 0 };
    str8             name = str8_from_cstr(ent->d_name);
    e.name = str8_dup_alloc(name, op->alloc);
    if (name.len > 0 && !e.name.data)
        return false;
    e.kind = posix_entry_kind(ent);

    if (op->stat_entries || e.kind == MEL_FS_KIND_NONE)
    {
        char        child[4096];
        int         n = snprintf(child, sizeof child, "%s/%s", dir, ent->d_name);
        struct stat st;
        if (n > 0 && (usize)n < sizeof child && stat(child, &st) == 0)
        {
            e.kind = posix_kind_from_mode(st.st_mode);
            e.size_bytes = (u64)st.st_size;
#if defined(__APPLE__)
            e.mtime_ns = posix_timespec_ns(st.st_mtimespec);
#else
            e.mtime_ns = posix_timespec_ns(st.st_mtim);
#endif
        }
    }
    mel_array_push(arr, e);
    return true;
}

void mel_fs__do_enumerate(Mel_Fs_Op_Record* op)
{
    const char* path = str8_to_cstr(op->path_a, op->alloc);
    DIR*        d = opendir(path);
    if (!d)
    {
        int e = errno;
        mel_dealloc(op->alloc, (void*)path);
        op->result.dir.status = mel_fs__status_from_errno(e);
        op->result.dir.os_error = e;
        return;
    }

    Fs_Entry_Array arr;
    mel_array_init(&arr, op->alloc);
    Fs_Entry_Array batch;
    mel_array_init(&batch, op->alloc);

    struct dirent* ent;
    int            e = 0;
    u32            bcap = op->batch == 0 ? 64 : op->batch;
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (!dir_push_entry(op, &arr, path, ent))
        {
            e = ENOMEM;
            break;
        }
        if (op->on_batch)
        {
            mel_array_push(&batch, arr.items[arr.count - 1]);
            if (batch.count >= bcap)
            {
                op->on_batch(batch.items, (u32)batch.count, op->stream_user);
                mel_array_clear(&batch);
            }
        }
    }
    if (op->on_batch && batch.count > 0)
        op->on_batch(batch.items, (u32)batch.count, op->stream_user);
    mel_array_free(&batch);
    closedir(d);
    mel_dealloc(op->alloc, (void*)path);

    if (e != 0)
    {
        for (usize i = 0; i < arr.count; i++)
            if (arr.items[i].name.data)
                mel_dealloc(op->alloc, arr.items[i].name.data);
        mel_array_free(&arr);
        op->result.dir.status = mel_fs__status_from_errno(e);
        op->result.dir.os_error = e;
        return;
    }
    op->result.dir.entries = arr.items;
    op->result.dir.count = (u32)arr.count;
    op->result.dir.status = MEL_FS_OK;
    op->result.dir.os_error = 0;
}

static int glob_walk(Mel_Fs_Op_Record* op, const char* dir, str8 dir_str, Fs_Entry_Array* arr)
{
    DIR* d = opendir(dir);
    if (!d)
        return errno == ENOENT ? 0 : errno;
    struct dirent* ent;
    int            e = 0;
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        str8 name = str8_from_cstr(ent->d_name);
        char child[4096];
        int  n = snprintf(child, sizeof child, "%s/%s", dir, ent->d_name);
        if (n < 0 || (usize)n >= sizeof child)
            continue;

        bool        is_dir = posix_entry_is_dir(ent, child);
        struct stat st;

        if (mel_fs_glob_match(op->glob_pattern, name, op->case_insensitive))
        {
            Mel_Fs_Dir_Entry me = { 0 };
            str8             rel;
            if (dir_str.len > 0)
            {
                char relbuf[4096];
                int  rn = snprintf(relbuf, sizeof relbuf, "%.*s/%s", (int)dir_str.len, dir_str.data, ent->d_name);
                rel = (rn > 0 && (usize)rn < sizeof relbuf) ? str8_from_parts((u8*)relbuf, rn) : name;
                me.name = str8_dup_alloc(rel, op->alloc);
            }
            else
            {
                me.name = str8_dup_alloc(name, op->alloc);
            }
            if (stat(child, &st) == 0)
            {
                me.kind = posix_kind_from_mode(st.st_mode);
                me.size_bytes = (u64)st.st_size;
#if defined(__APPLE__)
                me.mtime_ns = posix_timespec_ns(st.st_mtimespec);
#else
                me.mtime_ns = posix_timespec_ns(st.st_mtim);
#endif
            }
            mel_array_push(arr, me);
        }

        if (op->recursive && is_dir)
        {
            char subrel[4096];
            str8 sub;
            if (dir_str.len > 0)
            {
                int rn = snprintf(subrel, sizeof subrel, "%.*s/%s", (int)dir_str.len, dir_str.data, ent->d_name);
                sub = (rn > 0 && (usize)rn < sizeof subrel) ? str8_from_parts((u8*)subrel, rn) : name;
            }
            else
            {
                sub = name;
            }
            e = glob_walk(op, child, sub, arr);
            if (e != 0)
                break;
        }
    }
    closedir(d);
    return e;
}

void mel_fs__do_glob(Mel_Fs_Op_Record* op)
{
    const char*    root = str8_to_cstr(op->path_a, op->alloc);
    Fs_Entry_Array arr;
    mel_array_init(&arr, op->alloc);
    int e = glob_walk(op, root, STR8_EMPTY, &arr);
    mel_dealloc(op->alloc, (void*)root);

    if (e != 0)
    {
        for (usize i = 0; i < arr.count; i++)
            if (arr.items[i].name.data)
                mel_dealloc(op->alloc, arr.items[i].name.data);
        mel_array_free(&arr);
        op->result.dir.status = mel_fs__status_from_errno(e);
        op->result.dir.os_error = e;
        return;
    }
    op->result.dir.entries = arr.items;
    op->result.dir.count = (u32)arr.count;
    op->result.dir.status = MEL_FS_OK;
    op->result.dir.os_error = 0;
}

Mel_Fs_Path_Result mel_fs__backend_cwd(const Mel_Alloc* alloc)
{
    Mel_Fs_Path_Result r = { 0 };
    char               buf[4096];
    if (!getcwd(buf, sizeof buf))
    {
        int e = errno;
        r.status = mel_fs__status_from_errno(e);
        r.os_error = e;
        return r;
    }
    r.value = str8_dup_alloc(str8_from_cstr(buf), alloc);
    r.status = MEL_FS_OK;
    return r;
}

Mel_Fs_Void_Result mel_fs__backend_chdir(str8 path)
{
    Mel_Fs_Void_Result r = { 0 };
    char               buf[4096];
    if (path.len >= sizeof buf)
    {
        r.status = MEL_FS_ERROR | MEL_FS_NAME_TOO_LONG;
        r.os_error = ENAMETOOLONG;
        return r;
    }
    memcpy(buf, path.data, (usize)path.len);
    buf[path.len] = '\0';
    if (chdir(buf) != 0)
    {
        int e = errno;
        r.status = mel_fs__status_from_errno(e);
        r.os_error = e;
        return r;
    }
    r.status = MEL_FS_OK;
    return r;
}
