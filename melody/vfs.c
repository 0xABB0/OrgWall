#include "vfs.h"
#include "string.str8.h"
#include "string.path.h"

#include <string.h>

void mel_vfs_mount(Mel_Vfs* vfs, str8 prefix, Mel_Vfs_Backend* backend, u8 priority, bool writable)
{
    assert(vfs);
    assert(backend);
    assert(prefix.len > 0);
    assert(vfs->state_lock);

    u8 norm_buf[MEL_VFS_MAX_PATH];
    str8 np = mel_path_normalize(prefix, norm_buf, sizeof(norm_buf));
    assert(np.len > 0);

    u8* duped = mel_alloc(vfs->alloc, (usize)np.len);
    memcpy(duped, np.data, (usize)np.len);

    Mel_Vfs_Mount mount = {
        .prefix = { .data = duped, .len = np.len },
        .backend = backend,
        .priority = priority,
        .writable = writable,
        .refcount = 0,
        .retired = false,
        .insertion_order = 0,
    };

    SDL_LockMutex(vfs->state_lock);
    mount.insertion_order = vfs->mount_insertion_counter++;
    mel_array_push(&vfs->mounts, mount);
    vfs->mount_generation++;
    SDL_UnlockMutex(vfs->state_lock);
}

void mel_vfs_unmount(Mel_Vfs* vfs, str8 prefix)
{
    assert(vfs);
    assert(vfs->state_lock);

    u8 norm_buf[MEL_VFS_MAX_PATH];
    str8 np = mel_path_normalize(prefix, norm_buf, sizeof(norm_buf));

    SDL_LockMutex(vfs->state_lock);
    for (usize i = 0; i < vfs->mounts.count; i++) {
        Mel_Vfs_Mount* m = &vfs->mounts.items[i];
        if (m->retired) continue;
        if (m->prefix.len != np.len) continue;
        if (memcmp(m->prefix.data, np.data, (usize)np.len) != 0) continue;

        m->retired = true;
        vfs->mount_generation++;

        if (m->refcount == 0) {
            mel_dealloc(vfs->alloc, m->prefix.data);
            m->prefix = (str8){0};
        }
        SDL_UnlockMutex(vfs->state_lock);
        return;
    }
    SDL_UnlockMutex(vfs->state_lock);
}

static bool mel__vfs_blocking_op(Mel_Vfs* vfs, Mel_Vfs_Sqe* sqe, Mel_Vfs_Cqe* out_cqe)
{
    u64 ticket = mel_vfs_next_ticket(vfs);
    sqe->ticket = ticket;

    i32 accepted = mel_vfs_submit(vfs, sqe, 1);
    if (accepted < 1) return false;

    return mel_vfs_wait_ticket(vfs, ticket, UINT32_MAX, out_cqe);
}

u8* mel_vfs_read_file_alloc(Mel_Vfs* vfs, str8 path, usize* out_size, const Mel_Alloc* alloc)
{
    assert(vfs);
    assert(alloc);

    Mel_Vfs_Sqe open_sqe = { .op = MEL_VFS_OP_OPEN, .open = { .path = path, .open_flags = MEL_VFS_OPEN_READ } };
    Mel_Vfs_Cqe open_cqe;
    if (!mel__vfs_blocking_op(vfs, &open_sqe, &open_cqe)) return NULL;
    if (open_cqe.status != MEL_VFS_STATUS_OK) return NULL;

    Mel_Vfs_File file = open_cqe.file;

    Mel_Vfs_Sqe stat_sqe = { .op = MEL_VFS_OP_STAT, .stat = { .path = path } };
    Mel_Vfs_Cqe stat_cqe;
    if (!mel__vfs_blocking_op(vfs, &stat_sqe, &stat_cqe) || stat_cqe.status != MEL_VFS_STATUS_OK) {
        Mel_Vfs_Sqe close_sqe = { .op = MEL_VFS_OP_CLOSE, .close = { .file = file } };
        Mel_Vfs_Cqe close_cqe;
        mel__vfs_blocking_op(vfs, &close_sqe, &close_cqe);
        return NULL;
    }

    usize file_size = (usize)stat_cqe.stat.size;
    u8* buf = mel_alloc(alloc, file_size + 1);

    Mel_Vfs_Sqe read_sqe = { .op = MEL_VFS_OP_READ, .read = { .file = file, .offset = 0, .dst = buf, .size = file_size } };
    Mel_Vfs_Cqe read_cqe;
    if (!mel__vfs_blocking_op(vfs, &read_sqe, &read_cqe) || read_cqe.status != MEL_VFS_STATUS_OK) {
        mel_dealloc(alloc, buf);
        Mel_Vfs_Sqe close_sqe = { .op = MEL_VFS_OP_CLOSE, .close = { .file = file } };
        Mel_Vfs_Cqe close_cqe;
        mel__vfs_blocking_op(vfs, &close_sqe, &close_cqe);
        return NULL;
    }

    buf[read_cqe.result] = 0;
    if (out_size) *out_size = (usize)read_cqe.result;

    Mel_Vfs_Sqe close_sqe = { .op = MEL_VFS_OP_CLOSE, .close = { .file = file } };
    Mel_Vfs_Cqe close_cqe;
    mel__vfs_blocking_op(vfs, &close_sqe, &close_cqe);

    return buf;
}

str8 mel_vfs_read_text_alloc(Mel_Vfs* vfs, str8 path, const Mel_Alloc* alloc)
{
    usize size = 0;
    u8* data = mel_vfs_read_file_alloc(vfs, path, &size, alloc);
    if (!data) return (str8){0};
    return (str8){ .data = data, .len = (size_t)size };
}

bool mel_vfs_write_file(Mel_Vfs* vfs, str8 path, const u8* data, usize size)
{
    assert(vfs);

    Mel_Vfs_Sqe open_sqe = {
        .op = MEL_VFS_OP_OPEN,
        .open = { .path = path, .open_flags = MEL_VFS_OPEN_WRITE | MEL_VFS_OPEN_CREATE | MEL_VFS_OPEN_TRUNCATE },
    };
    Mel_Vfs_Cqe open_cqe;
    if (!mel__vfs_blocking_op(vfs, &open_sqe, &open_cqe)) return false;
    if (open_cqe.status != MEL_VFS_STATUS_OK) return false;

    Mel_Vfs_File file = open_cqe.file;

    Mel_Vfs_Sqe write_sqe = { .op = MEL_VFS_OP_WRITE, .write = { .file = file, .offset = 0, .src = data, .size = size } };
    Mel_Vfs_Cqe write_cqe;
    bool ok = mel__vfs_blocking_op(vfs, &write_sqe, &write_cqe)
           && write_cqe.status == MEL_VFS_STATUS_OK
           && write_cqe.result == (i64)size;

    Mel_Vfs_Sqe close_sqe = { .op = MEL_VFS_OP_CLOSE, .close = { .file = file } };
    Mel_Vfs_Cqe close_cqe;
    mel__vfs_blocking_op(vfs, &close_sqe, &close_cqe);

    return ok;
}

bool mel_vfs_write_text(Mel_Vfs* vfs, str8 path, str8 text)
{
    return mel_vfs_write_file(vfs, path, text.data, (usize)text.len);
}

bool mel_vfs_exists(Mel_Vfs* vfs, str8 path)
{
    Mel_Vfs_Stat st;
    return mel_vfs_stat_sync(vfs, path, &st);
}

bool mel_vfs_stat_sync(Mel_Vfs* vfs, str8 path, Mel_Vfs_Stat* out)
{
    assert(vfs);
    assert(out);

    Mel_Vfs_Sqe sqe = { .op = MEL_VFS_OP_STAT, .stat = { .path = path } };
    Mel_Vfs_Cqe cqe;
    if (!mel__vfs_blocking_op(vfs, &sqe, &cqe)) return false;
    if (cqe.status != MEL_VFS_STATUS_OK) return false;
    *out = cqe.stat;
    return true;
}

bool mel_vfs_sync_file(Mel_Vfs* vfs, Mel_Vfs_File file)
{
    assert(vfs);

    Mel_Vfs_Sqe sqe = { .op = MEL_VFS_OP_SYNC, .sync = { .file = file } };
    Mel_Vfs_Cqe cqe;
    if (!mel__vfs_blocking_op(vfs, &sqe, &cqe)) return false;
    return cqe.status == MEL_VFS_STATUS_OK;
}

bool mel_vfs_rename(Mel_Vfs* vfs, str8 src, str8 dst)
{
    assert(vfs);

    Mel_Vfs_Sqe sqe = { .op = MEL_VFS_OP_RENAME, .rename = { .src_path = src, .dst_path = dst } };
    Mel_Vfs_Cqe cqe;
    if (!mel__vfs_blocking_op(vfs, &sqe, &cqe)) return false;
    return cqe.status == MEL_VFS_STATUS_OK;
}

bool mel_vfs_delete(Mel_Vfs* vfs, str8 path)
{
    assert(vfs);

    Mel_Vfs_Sqe sqe = { .op = MEL_VFS_OP_DELETE, .del = { .path = path } };
    Mel_Vfs_Cqe cqe;
    if (!mel__vfs_blocking_op(vfs, &sqe, &cqe)) return false;
    return cqe.status == MEL_VFS_STATUS_OK;
}

bool mel_vfs_mkdir(Mel_Vfs* vfs, str8 path)
{
    assert(vfs);

    Mel_Vfs_Sqe sqe = { .op = MEL_VFS_OP_MKDIR, .mkdir = { .path = path } };
    Mel_Vfs_Cqe cqe;
    if (!mel__vfs_blocking_op(vfs, &sqe, &cqe)) return false;
    return cqe.status == MEL_VFS_STATUS_OK;
}

static bool mel__vfs_enumerate_dir(Mel_Vfs* vfs, str8 base_path, Mel_Vfs_Enum_Cb cb, void* user, Mel_Vfs_Enum_Opt opt)
{
    Mel_Vfs_Sqe open_sqe = { .op = MEL_VFS_OP_DIR_OPEN, .dir_open = { .path = base_path } };
    Mel_Vfs_Cqe open_cqe;
    if (!mel__vfs_blocking_op(vfs, &open_sqe, &open_cqe)) return false;
    if (open_cqe.status != MEL_VFS_STATUS_OK) return false;

    Mel_Vfs_Dir dir = open_cqe.dir;
    u8 name_buf[MEL_VFS_MAX_PATH];
    Mel_Vfs_Dir_Entry entry;
    bool ok = true;
    bool stopped = false;

    for (;;) {
        Mel_Vfs_Sqe next_sqe = {
            .op = MEL_VFS_OP_DIR_NEXT,
            .dir_next = {
                .dir = dir,
                .entry = &entry,
                .name_buf = name_buf,
                .name_cap = sizeof(name_buf),
            },
        };
        Mel_Vfs_Cqe next_cqe;
        if (!mel__vfs_blocking_op(vfs, &next_sqe, &next_cqe)) { ok = false; break; }
        if (next_cqe.status == MEL_VFS_STATUS_EOF) break;
        if (next_cqe.status != MEL_VFS_STATUS_OK) { ok = false; break; }

        bool is_dir = (entry.stat.flags & MEL_VFS_STAT_IS_DIR) != 0;

        u8 full_buf[MEL_VFS_MAX_PATH];
        str8 full_path = mel_path_join(base_path, entry.name, full_buf, sizeof(full_buf));

        if (is_dir) {
            if (opt.include_dirs) {
                if (!cb(full_path, &entry.stat, user)) { stopped = true; break; }
            }
            if (opt.recursive) {
                if (!mel__vfs_enumerate_dir(vfs, full_path, cb, user, opt)) { ok = false; break; }
            }
        } else {
            if (!cb(full_path, &entry.stat, user)) { stopped = true; break; }
        }
    }

    Mel_Vfs_Sqe close_sqe = { .op = MEL_VFS_OP_DIR_CLOSE, .dir_close = { .dir = dir } };
    Mel_Vfs_Cqe close_cqe;
    mel__vfs_blocking_op(vfs, &close_sqe, &close_cqe);

    (void)stopped;
    return ok;
}

bool mel_vfs_enumerate_opt(Mel_Vfs* vfs, str8 path, Mel_Vfs_Enum_Cb cb, void* user, Mel_Vfs_Enum_Opt opt)
{
    assert(vfs);
    assert(cb);
    return mel__vfs_enumerate_dir(vfs, path, cb, user, opt);
}
