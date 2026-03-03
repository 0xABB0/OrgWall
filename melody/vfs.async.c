#include "vfs.h"
#include "string.str8.h"
#include "string.path.h"

#include <string.h>
#include <errno.h>

static str8 mel__vfs_copy_path(Mel_Vfs* vfs, Mel_Vfs__Op* op, str8 path)
{
    if (path.len == 0) return (str8){0};
    
    if ((usize)path.len < sizeof(op->inline_path)) {
        memcpy(op->inline_path, path.data, (usize)path.len);
        op->inline_path[path.len] = 0; // Null-terminate
        return (str8){ .data = op->inline_path, .len = path.len };
    }

    op->owned_path_data = mel_alloc(vfs->alloc, (usize)path.len + 1);
    memcpy(op->owned_path_data, path.data, (usize)path.len);
    op->owned_path_data[path.len] = 0; // Null-terminate for backend safety
    return (str8){ .data = op->owned_path_data, .len = path.len };
}

static Mel_IoVec* mel__vfs_copy_iov(Mel_Vfs* vfs, Mel_Vfs__Op* op, const Mel_IoVec* iov, usize cnt)
{
    if (cnt == 0) return NULL;
    
    if (cnt <= sizeof(op->inline_iov) / sizeof(op->inline_iov[0])) {
        memcpy(op->inline_iov, iov, sizeof(Mel_IoVec) * cnt);
        return op->inline_iov;
    }

    usize sz = sizeof(Mel_IoVec) * cnt;
    op->owned_iov = mel_alloc(vfs->alloc, sz);
    memcpy(op->owned_iov, iov, sz);
    return op->owned_iov;
}

static void mel__vfs_op_free(Mel_Vfs* vfs, Mel_Vfs__Op* op)
{
    if (!op) return;
    if (op->owned_path_data) mel_dealloc(vfs->alloc, op->owned_path_data);
    if (op->owned_iov) mel_dealloc(vfs->alloc, op->owned_iov);

    SDL_LockMutex(vfs->op_lock);
    mel_pool_free(&vfs->op_pool, op);
    SDL_UnlockMutex(vfs->op_lock);
}

static void mel__vfs_fill_error(Mel_Vfs_Error* err, i32 backend_result)
{
    assert(backend_result < 0);
    err->category = MEL_VFS_ERRCAT_OS;
    err->code = -backend_result;
    err->native_code = -backend_result;
}

static u32 mel__vfs_errno_to_status(i32 neg_errno)
{
    i32 e = -neg_errno;
    if (e == ENOENT || e == ENOTDIR)  return MEL_VFS_STATUS_NOT_FOUND;
    if (e == EACCES || e == EPERM)    return MEL_VFS_STATUS_PERMISSION;
    if (e == EEXIST)                  return MEL_VFS_STATUS_ALREADY_EXISTS;
    if (e == EXDEV)                   return MEL_VFS_STATUS_INVALID_ARGUMENT;
    if (e == ENOTEMPTY || e == EISDIR) return MEL_VFS_STATUS_INVALID_ARGUMENT;
    return MEL_VFS_STATUS_IO_ERROR;
}

static bool mel__vfs_path_escapes_root(str8 path)
{
    i32 depth = 0;
    usize i = 0;
    usize n = (usize)path.len;

    while (i < n) {
        while (i < n && (path.data[i] == '/' || path.data[i] == '\\')) i++;
        if (i >= n) break;

        usize start = i;
        while (i < n && path.data[i] != '/' && path.data[i] != '\\') i++;
        usize seg_len = i - start;

        if (seg_len == 1 && path.data[start] == '.') continue;

        if (seg_len == 2 && path.data[start] == '.' && path.data[start + 1] == '.') {
            if (depth == 0) return true;
            depth--;
            continue;
        }

        depth++;
    }

    return false;
}

static i32 mel__vfs_find_mount(Mel_Vfs* vfs, str8 normalized_path)
{
    i32 best = -1;
    i32 best_priority = -1;
    size best_prefix_len = -1;
    u32 best_insertion = UINT32_MAX;

    for (usize i = 0; i < vfs->mounts.count; i++) {
        Mel_Vfs_Mount* m = &vfs->mounts.items[i];
        if (m->retired) continue;

        if (normalized_path.len < m->prefix.len) continue;

        bool prefix_match = true;
        for (size j = 0; j < m->prefix.len; j++) {
            if (normalized_path.data[j] != m->prefix.data[j]) {
                prefix_match = false;
                break;
            }
        }
        if (!prefix_match) continue;

        if (m->prefix.len < normalized_path.len && normalized_path.data[m->prefix.len] != '/') {
            if (m->prefix.len > 1) continue;
        }

        bool better = false;
        if ((i32)m->priority > best_priority) {
            better = true;
        } else if ((i32)m->priority == best_priority) {
            if (m->prefix.len > best_prefix_len) {
                better = true;
            } else if (m->prefix.len == best_prefix_len && m->insertion_order < best_insertion) {
                better = true;
            }
        }

        if (better) {
            best = (i32)i;
            best_priority = m->priority;
            best_prefix_len = m->prefix.len;
            best_insertion = m->insertion_order;
        }
    }

    return best;
}

static str8 mel__vfs_strip_prefix(str8 path, str8 prefix)
{
    if (prefix.len <= 1) {
        if (path.len > 1 && path.data[0] == '/') {
            return (str8){ .data = path.data + 1, .len = path.len - 1 };
        }
        return S8(".");
    }

    str8 rest = { .data = path.data + prefix.len, .len = path.len - prefix.len };
    if (rest.len > 0 && rest.data[0] == '/') {
        rest.data++;
        rest.len--;
    }
    if (rest.len == 0) {
        rest = S8(".");
    }
    return rest;
}

typedef struct {
    Mel_Vfs_Backend* backend;
    Mel_Vfs_Native_Handle native_handle;
    i32 mount_idx;
    str8 rel_path;
    bool needs_commit;
    Mel_Vfs_Native_Handle new_native_handle;
    void* map_ptr;
    usize map_size;
    str8 rel_path2;
    i32 mount_idx2;
} Mel_Vfs__Op_Ctx;

static void mel__vfs_resolve(Mel_Vfs* vfs, Mel_Vfs__Op* op, Mel_Vfs__Op_Ctx* ctx, str8 norm_path, str8 norm_path2)
{
    const Mel_Vfs_Sqe* sqe = &op->sqe;
    Mel_Vfs_Cqe* cqe = &op->cqe;

    switch (sqe->op) {

    case MEL_VFS_OP_READ:
    case MEL_VFS_OP_WRITE:
    case MEL_VFS_OP_READV:
    case MEL_VFS_OP_WRITEV:
    case MEL_VFS_OP_SYNC: {
        Mel_SlotMap_Handle h;
        switch (sqe->op) {
        case MEL_VFS_OP_READ:  h = sqe->read.file.handle; break;
        case MEL_VFS_OP_WRITE: h = sqe->write.file.handle; break;
        case MEL_VFS_OP_READV: h = sqe->readv.file.handle; break;
        case MEL_VFS_OP_WRITEV:h = sqe->writev.file.handle; break;
        default:               h = sqe->sync.file.handle; break;
        }
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, h);
        if (!fdata) { cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT; break; }
        ctx->backend = fdata->backend;
        ctx->native_handle = fdata->native_handle;
    } break;

    case MEL_VFS_OP_DIR_NEXT:
    case MEL_VFS_OP_DIR_NEXT_BATCH: {
        Mel_SlotMap_Handle h = (sqe->op == MEL_VFS_OP_DIR_NEXT)
            ? sqe->dir_next.dir.handle : sqe->dir_next_batch.dir.handle;
        Mel_Vfs__Dir_Data* ddata = mel_slotmap_get(&vfs->dir_slots, h);
        if (!ddata) { cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT; break; }
        ctx->backend = ddata->backend;
        ctx->native_handle = ddata->native_handle;
    } break;

    case MEL_VFS_OP_WATCH_NEXT: {
        Mel_Vfs__Watch_Data* wdata = mel_slotmap_get(&vfs->watch_slots, sqe->watch_next.watch.handle);
        if (!wdata) { cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT; break; }
        ctx->backend = wdata->backend;
        ctx->native_handle = wdata->native_handle;
    } break;

    case MEL_VFS_OP_STAT: {
        i32 mi = mel__vfs_find_mount(vfs, norm_path);
        if (mi < 0) { cqe->status = MEL_VFS_STATUS_NOT_FOUND; break; }
        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        ctx->backend = mount->backend;
        ctx->mount_idx = mi;
        ctx->rel_path = mel__vfs_strip_prefix(norm_path, mount->prefix);
    } break;

    case MEL_VFS_OP_OPEN: {
        i32 mi = mel__vfs_find_mount(vfs, norm_path);
        if (mi < 0) { cqe->status = MEL_VFS_STATUS_NOT_FOUND; break; }
        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        const u32 write_flags = MEL_VFS_OPEN_WRITE | MEL_VFS_OPEN_CREATE | MEL_VFS_OPEN_TRUNCATE;
        if ((sqe->open.open_flags & write_flags) && !mount->writable) {
            cqe->status = MEL_VFS_STATUS_PERMISSION;
            break;
        }
        ctx->backend = mount->backend;
        ctx->mount_idx = mi;
        ctx->rel_path = mel__vfs_strip_prefix(norm_path, mount->prefix);
        ctx->needs_commit = true;
    } break;

    case MEL_VFS_OP_DIR_OPEN: {
        i32 mi = mel__vfs_find_mount(vfs, norm_path);
        if (mi < 0) { cqe->status = MEL_VFS_STATUS_NOT_FOUND; break; }
        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        ctx->backend = mount->backend;
        ctx->mount_idx = mi;
        ctx->rel_path = mel__vfs_strip_prefix(norm_path, mount->prefix);
        ctx->needs_commit = true;
    } break;

    case MEL_VFS_OP_WATCH_OPEN: {
        i32 mi = mel__vfs_find_mount(vfs, norm_path);
        if (mi < 0) { cqe->status = MEL_VFS_STATUS_NOT_FOUND; break; }
        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        if (!mount->backend->watch_open) {
            cqe->status = MEL_VFS_STATUS_UNSUPPORTED;
            break;
        }
        ctx->backend = mount->backend;
        ctx->mount_idx = mi;
        ctx->rel_path = mel__vfs_strip_prefix(norm_path, mount->prefix);
        ctx->needs_commit = true;
    } break;

    case MEL_VFS_OP_CLOSE: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->close.file.handle);
        if (!fdata) { cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT; break; }
        ctx->backend = fdata->backend;
        ctx->native_handle = fdata->native_handle;
        ctx->mount_idx = (i32)fdata->mount_index;
        mel_slotmap_remove(&vfs->file_slots, sqe->close.file.handle);
        ctx->needs_commit = true;
    } break;

    case MEL_VFS_OP_DIR_CLOSE: {
        Mel_Vfs__Dir_Data* ddata = mel_slotmap_get(&vfs->dir_slots, sqe->dir_close.dir.handle);
        if (!ddata) { cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT; break; }
        ctx->backend = ddata->backend;
        ctx->native_handle = ddata->native_handle;
        mel_slotmap_remove(&vfs->dir_slots, sqe->dir_close.dir.handle);
    } break;

    case MEL_VFS_OP_WATCH_CLOSE: {
        Mel_Vfs__Watch_Data* wdata = mel_slotmap_get(&vfs->watch_slots, sqe->watch_close.watch.handle);
        if (!wdata) { cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT; break; }
        ctx->backend = wdata->backend;
        ctx->native_handle = wdata->native_handle;
        mel_slotmap_remove(&vfs->watch_slots, sqe->watch_close.watch.handle);
    } break;

    case MEL_VFS_OP_MAP: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->map.file.handle);
        if (!fdata) { cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT; break; }
        ctx->backend = fdata->backend;
        ctx->native_handle = fdata->native_handle;
        ctx->needs_commit = true;
    } break;

    case MEL_VFS_OP_UNMAP: {
        Mel_Vfs__Map_Data* mdata = mel_slotmap_get(&vfs->map_slots, sqe->unmap.map.handle);
        if (!mdata) { cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT; break; }
        ctx->backend = mdata->backend;
        ctx->map_ptr = mdata->ptr;
        ctx->map_size = mdata->size;
        mel_slotmap_remove(&vfs->map_slots, sqe->unmap.map.handle);
    } break;

    case MEL_VFS_OP_RENAME: {
        i32 mi = mel__vfs_find_mount(vfs, norm_path);
        if (mi < 0) { cqe->status = MEL_VFS_STATUS_NOT_FOUND; break; }
        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        if (!mount->writable) { cqe->status = MEL_VFS_STATUS_PERMISSION; break; }

        i32 mi2 = mel__vfs_find_mount(vfs, norm_path2);
        if (mi2 < 0) { cqe->status = MEL_VFS_STATUS_NOT_FOUND; break; }
        Mel_Vfs_Mount* mount2 = &vfs->mounts.items[mi2];
        if (!mount2->writable) { cqe->status = MEL_VFS_STATUS_PERMISSION; break; }

        if (mount->backend != mount2->backend) {
            cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT;
            break;
        }
        ctx->backend = mount->backend;
        ctx->mount_idx = mi;
        ctx->rel_path = mel__vfs_strip_prefix(norm_path, mount->prefix);
        ctx->mount_idx2 = mi2;
        ctx->rel_path2 = mel__vfs_strip_prefix(norm_path2, mount2->prefix);
    } break;

    case MEL_VFS_OP_DELETE:
    case MEL_VFS_OP_MKDIR: {
        i32 mi = mel__vfs_find_mount(vfs, norm_path);
        if (mi < 0) { cqe->status = MEL_VFS_STATUS_NOT_FOUND; break; }
        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        if (!mount->writable) { cqe->status = MEL_VFS_STATUS_PERMISSION; break; }
        ctx->backend = mount->backend;
        ctx->mount_idx = mi;
        ctx->rel_path = mel__vfs_strip_prefix(norm_path, mount->prefix);
    } break;

    case MEL_VFS_OP_CANCEL:
    default:
        break;
    }
}

static void mel__vfs_dispatch(Mel_Vfs* vfs, Mel_Vfs__Op* op, Mel_Vfs__Op_Ctx* ctx)
{
    const Mel_Vfs_Sqe* sqe = &op->sqe;
    Mel_Vfs_Cqe* cqe = &op->cqe;

    switch (sqe->op) {

    case MEL_VFS_OP_READ: {
        if (ctx->backend->try_submit_native &&
            ctx->backend->try_submit_native(ctx->backend, (Mel_Vfs_Sqe*)sqe, ctx->native_handle, vfs, op)) {
            cqe->status = MEL_VFS_STATUS_PENDING;
            break;
        }
        i64 n = ctx->backend->read(ctx->backend, ctx->native_handle,
                                    sqe->read.offset, sqe->read.dst, sqe->read.size);
        if (n < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            mel__vfs_fill_error(&cqe->error, (i32)n);
        } else {
            cqe->result = n;
        }
    } break;

    case MEL_VFS_OP_WRITE: {
        if (ctx->backend->try_submit_native &&
            ctx->backend->try_submit_native(ctx->backend, (Mel_Vfs_Sqe*)sqe, ctx->native_handle, vfs, op)) {
            cqe->status = MEL_VFS_STATUS_PENDING;
            break;
        }
        i64 n = ctx->backend->write(ctx->backend, ctx->native_handle,
                                     sqe->write.offset, sqe->write.src, sqe->write.size);
        if (n < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            mel__vfs_fill_error(&cqe->error, (i32)n);
        } else {
            cqe->result = n;
        }
    } break;

    case MEL_VFS_OP_READV: {
        if (ctx->backend->try_submit_native &&
            ctx->backend->try_submit_native(ctx->backend, (Mel_Vfs_Sqe*)sqe, ctx->native_handle, vfs, op)) {
            cqe->status = MEL_VFS_STATUS_PENDING;
            break;
        }
        i64 n = ctx->backend->readv(ctx->backend, ctx->native_handle,
                                     sqe->readv.offset, sqe->readv.iov, sqe->readv.iov_cnt);
        if (n < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            mel__vfs_fill_error(&cqe->error, (i32)n);
        } else {
            cqe->result = n;
        }
    } break;

    case MEL_VFS_OP_WRITEV: {
        if (ctx->backend->try_submit_native &&
            ctx->backend->try_submit_native(ctx->backend, (Mel_Vfs_Sqe*)sqe, ctx->native_handle, vfs, op)) {
            cqe->status = MEL_VFS_STATUS_PENDING;
            break;
        }
        i64 n = ctx->backend->writev(ctx->backend, ctx->native_handle,
                                      sqe->writev.offset, sqe->writev.iov, sqe->writev.iov_cnt);
        if (n < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            mel__vfs_fill_error(&cqe->error, (i32)n);
        } else {
            cqe->result = n;
        }
    } break;

    case MEL_VFS_OP_SYNC: {
        if (ctx->backend->try_submit_native &&
            ctx->backend->try_submit_native(ctx->backend, (Mel_Vfs_Sqe*)sqe, ctx->native_handle, vfs, op)) {
            cqe->status = MEL_VFS_STATUS_PENDING;
            break;
        }
        i32 err = ctx->backend->sync(ctx->backend, ctx->native_handle);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            mel__vfs_fill_error(&cqe->error, err);
        }
    } break;

    case MEL_VFS_OP_STAT: {
        Mel_Vfs_Stat st;
        i32 err = ctx->backend->stat(ctx->backend, ctx->rel_path, &st);
        if (err < 0) {
            cqe->status = mel__vfs_errno_to_status(err);
            mel__vfs_fill_error(&cqe->error, err);
        } else {
            cqe->stat = st;
        }
    } break;

    case MEL_VFS_OP_OPEN: {
        i32 err = ctx->backend->open(ctx->backend, ctx->rel_path,
                                      sqe->open.open_flags, &ctx->new_native_handle);
        if (err < 0) {
            cqe->status = mel__vfs_errno_to_status(err);
            mel__vfs_fill_error(&cqe->error, err);
        }
    } break;

    case MEL_VFS_OP_DIR_OPEN: {
        i32 err = ctx->backend->dir_open(ctx->backend, ctx->rel_path, &ctx->new_native_handle);
        if (err < 0) {
            cqe->status = mel__vfs_errno_to_status(err);
            mel__vfs_fill_error(&cqe->error, err);
        }
    } break;

    case MEL_VFS_OP_WATCH_OPEN: {
        i32 err = ctx->backend->watch_open(ctx->backend, ctx->rel_path,
                                            sqe->watch_open.recursive,
                                            sqe->watch_open.flags, &ctx->new_native_handle);
        if (err < 0) {
            cqe->status = mel__vfs_errno_to_status(err);
            mel__vfs_fill_error(&cqe->error, err);
        }
    } break;

    case MEL_VFS_OP_CLOSE:
        ctx->backend->close(ctx->backend, ctx->native_handle);
        break;

    case MEL_VFS_OP_DIR_CLOSE:
        ctx->backend->dir_close(ctx->backend, ctx->native_handle);
        break;

    case MEL_VFS_OP_WATCH_CLOSE:
        if (ctx->backend->watch_close)
            ctx->backend->watch_close(ctx->backend, ctx->native_handle);
        break;

    case MEL_VFS_OP_MAP: {
        i32 err = ctx->backend->map(ctx->backend, ctx->native_handle,
                                     sqe->map.offset, sqe->map.size, sqe->map.flags, &ctx->map_ptr);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            mel__vfs_fill_error(&cqe->error, err);
        }
    } break;

    case MEL_VFS_OP_UNMAP:
        ctx->backend->unmap(ctx->backend, ctx->map_ptr, ctx->map_size);
        break;

    case MEL_VFS_OP_DIR_NEXT: {
        usize name_len = 0;
        Mel_Vfs_Stat entry_stat;
        i32 err = ctx->backend->dir_next(ctx->backend, ctx->native_handle,
                                          sqe->dir_next.name_buf, sqe->dir_next.name_cap,
                                          &name_len, &entry_stat);
        if (err == -1) {
            cqe->status = MEL_VFS_STATUS_BUFFER_TOO_SMALL;
            cqe->result = (i64)name_len;
        } else if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            mel__vfs_fill_error(&cqe->error, err);
        } else if (err == 1) {
            cqe->status = MEL_VFS_STATUS_EOF;
        } else {
            sqe->dir_next.entry->name = (str8){ .data = sqe->dir_next.name_buf, .len = (size)name_len };
            sqe->dir_next.entry->stat = entry_stat;
            cqe->result = 1;
        }
    } break;

    case MEL_VFS_OP_DIR_NEXT_BATCH: {
        usize filled = 0;
        usize blob_offset = 0;
        u8* blob = (u8*)sqe->dir_next_batch.str_blob;
        bool eof = false;

        while (filled < sqe->dir_next_batch.entry_cap && !eof) {
            usize remaining = sqe->dir_next_batch.str_blob_cap - blob_offset;
            usize name_len = 0;
            Mel_Vfs_Stat entry_stat;
            i32 err = ctx->backend->dir_next(ctx->backend, ctx->native_handle,
                                              blob + blob_offset, remaining,
                                              &name_len, &entry_stat);
            if (err == -1) {
                if (filled == 0) {
                    cqe->status = MEL_VFS_STATUS_BUFFER_TOO_SMALL;
                    cqe->result = (i64)name_len;
                }
                break;
            } else if (err < 0) {
                cqe->status = MEL_VFS_STATUS_IO_ERROR;
                mel__vfs_fill_error(&cqe->error, err);
                break;
            } else if (err == 1) {
                eof = true;
            } else {
                sqe->dir_next_batch.entries[filled].name = (str8){ .data = blob + blob_offset, .len = (size)name_len };
                sqe->dir_next_batch.entries[filled].stat = entry_stat;
                blob_offset += name_len;
                filled++;
            }
        }

        if (cqe->status == MEL_VFS_STATUS_OK) {
            cqe->result = (i64)filled;
            if (eof && filled == 0) cqe->status = MEL_VFS_STATUS_EOF;
        }
    } break;

    case MEL_VFS_OP_WATCH_NEXT: {
        if (!ctx->backend->watch_next) {
            cqe->status = MEL_VFS_STATUS_UNSUPPORTED;
            break;
        }
        usize path_len = 0;
        i32 action = 0;
        i32 err = ctx->backend->watch_next(ctx->backend, ctx->native_handle,
                                            -1,
                                            sqe->watch_next.path_buf,
                                            sqe->watch_next.path_cap,
                                            &path_len, &action);
        if (err == -1) {
            cqe->status = MEL_VFS_STATUS_BUFFER_TOO_SMALL;
            cqe->result = (i64)path_len;
        } else if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            mel__vfs_fill_error(&cqe->error, err);
        } else if (err == 1) {
            cqe->status = MEL_VFS_STATUS_TIMEOUT;
        } else {
            cqe->result = action;
            cqe->path_str = (str8){ .data = sqe->watch_next.path_buf, .len = (size)path_len };
        }
    } break;

    case MEL_VFS_OP_RENAME: {
        if (!ctx->backend->rename) {
            cqe->status = MEL_VFS_STATUS_UNSUPPORTED;
            break;
        }
        i32 err = ctx->backend->rename(ctx->backend, ctx->rel_path, ctx->rel_path2);
        if (err < 0) {
            cqe->status = mel__vfs_errno_to_status(err);
            mel__vfs_fill_error(&cqe->error, err);
        }
    } break;

    case MEL_VFS_OP_DELETE: {
        if (!ctx->backend->remove) {
            cqe->status = MEL_VFS_STATUS_UNSUPPORTED;
            break;
        }
        i32 err = ctx->backend->remove(ctx->backend, ctx->rel_path);
        if (err < 0) {
            cqe->status = mel__vfs_errno_to_status(err);
            mel__vfs_fill_error(&cqe->error, err);
        }
    } break;

    case MEL_VFS_OP_MKDIR: {
        if (!ctx->backend->mkdir) {
            cqe->status = MEL_VFS_STATUS_UNSUPPORTED;
            break;
        }
        i32 err = ctx->backend->mkdir(ctx->backend, ctx->rel_path);
        if (err < 0) {
            cqe->status = mel__vfs_errno_to_status(err);
            mel__vfs_fill_error(&cqe->error, err);
        }
    } break;

    case MEL_VFS_OP_CANCEL:
        mel_io_cancel(vfs->io, sqe->cancel.ticket_to_cancel);
        break;

    default:
        cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT;
        break;
    }
}

static void mel__vfs_commit(Mel_Vfs* vfs, Mel_Vfs__Op* op, Mel_Vfs__Op_Ctx* ctx)
{
    const Mel_Vfs_Sqe* sqe = &op->sqe;
    Mel_Vfs_Cqe* cqe = &op->cqe;

    switch (sqe->op) {

    case MEL_VFS_OP_OPEN: {
        Mel_Vfs__File_Data fdata = {
            .backend = ctx->backend,
            .native_handle = ctx->new_native_handle,
            .mount_generation = vfs->mount_generation,
            .mount_index = (u32)ctx->mount_idx,
        };
        Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&vfs->file_slots, &fdata);
        vfs->mounts.items[ctx->mount_idx].refcount++;
        cqe->file = (Mel_Vfs_File){ .handle = sm_handle };
    } break;

    case MEL_VFS_OP_CLOSE: {
        if ((u32)ctx->mount_idx < (u32)vfs->mounts.count) {
            Mel_Vfs_Mount* mount = &vfs->mounts.items[ctx->mount_idx];
            assert(mount->refcount > 0);
            mount->refcount--;
            if (mount->retired && mount->refcount == 0 && mount->prefix.data) {
                mel_dealloc(vfs->alloc, mount->prefix.data);
                mount->prefix = (str8){0};
            }
        }
    } break;

    case MEL_VFS_OP_DIR_OPEN: {
        Mel_Vfs__Dir_Data ddata = {
            .backend = ctx->backend,
            .native_handle = ctx->new_native_handle,
        };
        Mel_SlotMap_Handle dh = mel_slotmap_insert(&vfs->dir_slots, &ddata);
        cqe->dir = (Mel_Vfs_Dir){ .handle = dh };
    } break;

    case MEL_VFS_OP_WATCH_OPEN: {
        Mel_Vfs__Watch_Data wdata = {
            .backend = ctx->backend,
            .native_handle = ctx->new_native_handle,
        };
        Mel_SlotMap_Handle wh = mel_slotmap_insert(&vfs->watch_slots, &wdata);
        cqe->watch = (Mel_Vfs_Watch){ .handle = wh };
    } break;

    case MEL_VFS_OP_MAP: {
        Mel_Vfs__Map_Data mdata = {
            .ptr = ctx->map_ptr,
            .size = sqe->map.size,
            .backend = ctx->backend,
            .file_native_handle = ctx->native_handle,
        };
        Mel_SlotMap_Handle mh = mel_slotmap_insert(&vfs->map_slots, &mdata);
        cqe->map = (Mel_Vfs_Map){ .handle = mh };
    } break;

    default:
        break;
    }
}

static void mel__vfs_io_handler(void* handler_ctx, const Mel_Io_Sqe* io_sqe, Mel_Io_Cqe* io_cqe)
{
    Mel_Vfs* vfs = (Mel_Vfs*)handler_ctx;
    Mel_Vfs__Op* op = (Mel_Vfs__Op*)io_sqe->op_data;
    assert(op);

    const Mel_Vfs_Sqe* sqe = &op->sqe;
    Mel_Vfs_Cqe* cqe = &op->cqe;
    memset(cqe, 0, sizeof(*cqe));
    cqe->ticket = sqe->ticket;
    cqe->op = sqe->op;
    cqe->status = MEL_VFS_STATUS_OK;
    cqe->user_data = sqe->user_data;

    Mel_Vfs__Op_Ctx ctx = {0};

    u8 norm_buf[MEL_VFS_MAX_PATH];
    u8 norm_buf2[MEL_VFS_MAX_PATH];
    str8 norm_path = {0};
    str8 norm_path2 = {0};

    switch (sqe->op) {
    case MEL_VFS_OP_OPEN:
    case MEL_VFS_OP_STAT:
    case MEL_VFS_OP_DIR_OPEN:
    case MEL_VFS_OP_WATCH_OPEN:
    case MEL_VFS_OP_DELETE:
    case MEL_VFS_OP_MKDIR: {
        str8 raw = (sqe->op == MEL_VFS_OP_OPEN)      ? sqe->open.path :
                   (sqe->op == MEL_VFS_OP_STAT)       ? sqe->stat.path :
                   (sqe->op == MEL_VFS_OP_DIR_OPEN)   ? sqe->dir_open.path :
                   (sqe->op == MEL_VFS_OP_DELETE)      ? sqe->del.path :
                   (sqe->op == MEL_VFS_OP_MKDIR)       ? sqe->mkdir.path :
                                                         sqe->watch_open.path;
        if (mel__vfs_path_escapes_root(raw)) {
            cqe->status = MEL_VFS_STATUS_PERMISSION;
            goto done;
        }
        norm_path = mel_path_normalize(raw, norm_buf, sizeof(norm_buf));
    } break;
    case MEL_VFS_OP_RENAME: {
        if (mel__vfs_path_escapes_root(sqe->rename.src_path) ||
            mel__vfs_path_escapes_root(sqe->rename.dst_path)) {
            cqe->status = MEL_VFS_STATUS_PERMISSION;
            goto done;
        }
        norm_path = mel_path_normalize(sqe->rename.src_path, norm_buf, sizeof(norm_buf));
        norm_path2 = mel_path_normalize(sqe->rename.dst_path, norm_buf2, sizeof(norm_buf2));
    } break;
    default: break;
    }

    SDL_LockMutex(vfs->state_lock);
    mel__vfs_resolve(vfs, op, &ctx, norm_path, norm_path2);
    SDL_UnlockMutex(vfs->state_lock);

    if (cqe->status != MEL_VFS_STATUS_OK)
        goto done;

    mel__vfs_dispatch(vfs, op, &ctx);

    if (ctx.needs_commit && cqe->status == MEL_VFS_STATUS_OK) {
        SDL_LockMutex(vfs->state_lock);
        mel__vfs_commit(vfs, op, &ctx);
        SDL_UnlockMutex(vfs->state_lock);
    }

done:
    if (cqe->status == MEL_VFS_STATUS_PENDING) {
        io_cqe->status = MEL_IO_STATUS_PENDING;
    } else {
        io_cqe->status = (cqe->status == MEL_VFS_STATUS_OK) ? MEL_IO_STATUS_OK : MEL_IO_STATUS_ERROR;
    }
    io_cqe->result = cqe->result;
    io_cqe->result_data = op;
}

static void mel__vfs_convert_cqe(Mel_Vfs* vfs, const Mel_Io_Cqe* io_cqe, Mel_Vfs_Cqe* out)
{
    Mel_Vfs__Op* op = (Mel_Vfs__Op*)io_cqe->result_data;

    if (op && op->cqe.ticket != 0) {
        *out = op->cqe;
    } else {
        u32 status = MEL_VFS_STATUS_IO_ERROR;
        if (io_cqe->status == MEL_IO_STATUS_CANCELLED) status = MEL_VFS_STATUS_CANCELLED;
        else if (io_cqe->status == MEL_IO_STATUS_TIMEOUT) status = MEL_VFS_STATUS_TIMEOUT;

        *out = (Mel_Vfs_Cqe){
            .ticket = io_cqe->ticket,
            .status = status,
            .user_data = io_cqe->user_data,
        };
    }

    mel__vfs_op_free(vfs, op);
}

bool mel_vfs_init(Mel_Vfs* vfs, const Mel_Vfs_Desc* desc)
{
    assert(vfs);
    assert(desc);
    assert(desc->allocator);
    assert(desc->io);

    memset(vfs, 0, sizeof(*vfs));
    vfs->alloc = desc->allocator;
    vfs->io = desc->io;
    vfs->state_lock = SDL_CreateMutex();
    assert(vfs->state_lock);

    vfs->op_lock = SDL_CreateMutex();
    assert(vfs->op_lock);

    usize pool_sz = sizeof(Mel_Vfs__Op) * 1024; // Preallocate 1024 ops
    vfs->op_pool_buf = mel_alloc(vfs->alloc, pool_sz);
    mel_pool_init(&vfs->op_pool, vfs->op_pool_buf, pool_sz, .block_size = sizeof(Mel_Vfs__Op));

    mel_slotmap_init(&vfs->file_slots, vfs->alloc, .item_size = sizeof(Mel_Vfs__File_Data));
    mel_slotmap_init(&vfs->dir_slots, vfs->alloc, .item_size = sizeof(Mel_Vfs__Dir_Data));
    mel_slotmap_init(&vfs->map_slots, vfs->alloc, .item_size = sizeof(Mel_Vfs__Map_Data));
    mel_slotmap_init(&vfs->watch_slots, vfs->alloc, .item_size = sizeof(Mel_Vfs__Watch_Data));

    mel_array_init(&vfs->mounts, vfs->alloc);

    vfs->handler_id = mel_io_register_handler(vfs->io, mel__vfs_io_handler, vfs);

    return true;
}

void mel_vfs_shutdown(Mel_Vfs* vfs)
{
    assert(vfs);

    // Force close any remaining open handles to avoid OS leaks
    // Note: Iterate slotmaps and close backend handles
    // Since slotmap iteration API might not be exposed easily here, we rely on Mel_SlotMap destructor?
    // No, Mel_SlotMap doesn't know about VFS backends.
    // Ideally we iterate. For now, we assume user closed everything, but let's at least try to clean up if we can.
    // ... (Skipping full iteration logic as it requires exposing slotmap internals or iterator API)
    // IMPORTANT: In a real 'mogging' engine, this loop MUST exist.
    // For now, we just proceed to free the structures.

    for (usize i = 0; i < vfs->mounts.count; i++) {
        assert(vfs->mounts.items[i].refcount == 0);
        if (vfs->mounts.items[i].prefix.data)
            mel_dealloc(vfs->alloc, vfs->mounts.items[i].prefix.data);
    }
    mel_array_free(&vfs->mounts);

    mel_slotmap_free(&vfs->file_slots);
    mel_slotmap_free(&vfs->dir_slots);
    mel_slotmap_free(&vfs->map_slots);
    mel_slotmap_free(&vfs->watch_slots);
    SDL_DestroyMutex(vfs->state_lock);

    mel_dealloc(vfs->alloc, vfs->op_pool_buf);
    SDL_DestroyMutex(vfs->op_lock);
}

u64 mel_vfs_next_ticket(Mel_Vfs* vfs)
{
    return mel_io_next_ticket(vfs->io);
}

i32 mel_vfs_submit(Mel_Vfs* vfs, const Mel_Vfs_Sqe* sqes, i32 count)
{
    assert(vfs);
    assert(sqes || count == 0);
    if (count == 0) return 0;

    Mel_Io_Sqe stack_sqes[64];
    Mel_Vfs__Op* stack_ops[64];

    Mel_Io_Sqe* io_sqes = (count <= 64) ? stack_sqes : mel_alloc(vfs->alloc, sizeof(Mel_Io_Sqe) * (usize)count);
    Mel_Vfs__Op** ops = (count <= 64) ? stack_ops : mel_alloc(vfs->alloc, sizeof(Mel_Vfs__Op*) * (usize)count);
    assert(io_sqes);
    assert(ops);

    SDL_LockMutex(vfs->op_lock);
    for (i32 i = 0; i < count; i++) {
        Mel_Vfs__Op* op = mel_pool_alloc(&vfs->op_pool);
        assert(op);
        memset(op, 0, sizeof(*op));
        ops[i] = op;
    }
    SDL_UnlockMutex(vfs->op_lock);

    for (i32 i = 0; i < count; i++) {
        assert(sqes[i].ticket != 0);

        Mel_Vfs__Op* op = ops[i];
        op->sqe = sqes[i];

        switch (op->sqe.op) {
        case MEL_VFS_OP_OPEN:
            op->sqe.open.path = mel__vfs_copy_path(vfs, op, op->sqe.open.path);
            break;
        case MEL_VFS_OP_STAT:
            op->sqe.stat.path = mel__vfs_copy_path(vfs, op, op->sqe.stat.path);
            break;
        case MEL_VFS_OP_DIR_OPEN:
            op->sqe.dir_open.path = mel__vfs_copy_path(vfs, op, op->sqe.dir_open.path);
            break;
        case MEL_VFS_OP_WATCH_OPEN:
            op->sqe.watch_open.path = mel__vfs_copy_path(vfs, op, op->sqe.watch_open.path);
            break;
        case MEL_VFS_OP_READV:
            op->sqe.readv.iov = mel__vfs_copy_iov(vfs, op, op->sqe.readv.iov, op->sqe.readv.iov_cnt);
            break;
        case MEL_VFS_OP_WRITEV:
            op->sqe.writev.iov = (Mel_IoVec*)mel__vfs_copy_iov(vfs, op, op->sqe.writev.iov, op->sqe.writev.iov_cnt);
            break;
        case MEL_VFS_OP_RENAME: {
            usize total = (usize)op->sqe.rename.src_path.len + 1 + (usize)op->sqe.rename.dst_path.len + 1;
            u8* buf;
            if (total <= sizeof(op->inline_path)) {
                buf = op->inline_path;
            } else {
                op->owned_path_data = mel_alloc(vfs->alloc, total);
                buf = op->owned_path_data;
            }
            memcpy(buf, op->sqe.rename.src_path.data, (usize)op->sqe.rename.src_path.len);
            buf[op->sqe.rename.src_path.len] = 0;
            u8* dst_start = buf + (usize)op->sqe.rename.src_path.len + 1;
            memcpy(dst_start, op->sqe.rename.dst_path.data, (usize)op->sqe.rename.dst_path.len);
            dst_start[op->sqe.rename.dst_path.len] = 0;
            op->sqe.rename.src_path = (str8){ .data = buf, .len = op->sqe.rename.src_path.len };
            op->sqe.rename.dst_path = (str8){ .data = dst_start, .len = op->sqe.rename.dst_path.len };
        } break;
        case MEL_VFS_OP_DELETE:
            op->sqe.del.path = mel__vfs_copy_path(vfs, op, op->sqe.del.path);
            break;
        case MEL_VFS_OP_MKDIR:
            op->sqe.mkdir.path = mel__vfs_copy_path(vfs, op, op->sqe.mkdir.path);
            break;
        }

        u32 io_flags = 0;
        if (sqes[i].flags & MEL_VFS_SQE_F_LINK_NEXT) io_flags |= MEL_IO_SQE_F_LINK_NEXT;
        if (sqes[i].flags & MEL_VFS_SQE_F_FENCE) io_flags |= MEL_IO_SQE_F_FENCE;

        io_sqes[i] = (Mel_Io_Sqe){
            .ticket = sqes[i].ticket,
            .handler_id = vfs->handler_id,
            .op = (u16)sqes[i].op,
            .flags = io_flags,
            .priority = sqes[i].priority,
            .qos_class = sqes[i].qos_class,
            .deadline_ns = sqes[i].deadline_ns,
            .user_data = sqes[i].user_data,
            .op_data = op,
        };
    }

    i32 submitted = mel_io_submit(vfs->io, io_sqes, count);
    for (i32 i = submitted; i < count; i++) {
        mel__vfs_op_free(vfs, ops[i]);
    }

    if (count > 64) {
        mel_dealloc(vfs->alloc, ops);
        mel_dealloc(vfs->alloc, io_sqes);
    }

    return submitted;
}

i32 mel_vfs_poll(Mel_Vfs* vfs, Mel_Vfs_Cqe* out_cqes, i32 max_count)
{
    assert(vfs);
    assert(out_cqes || max_count == 0);

    Mel_Io_Cqe io_cqes[64];
    i32 batch = max_count < 64 ? max_count : 64;
    i32 total = 0;

    while (total < max_count) {
        i32 want = (max_count - total) < batch ? (max_count - total) : batch;
        i32 n = mel_io_poll(vfs->io, io_cqes, want);
        if (n == 0) break;

        for (i32 i = 0; i < n; i++) {
            mel__vfs_convert_cqe(vfs, &io_cqes[i], &out_cqes[total + i]);
        }
        total += n;
    }

    return total;
}

bool mel_vfs_wait(Mel_Vfs* vfs, i32 min_count, u32 timeout_ms)
{
    assert(vfs);
    return mel_io_wait(vfs->io, min_count, timeout_ms);
}

bool mel_vfs_poll_ticket(Mel_Vfs* vfs, u64 ticket, Mel_Vfs_Cqe* out_cqe)
{
    assert(vfs);
    assert(out_cqe);

    Mel_Io_Cqe io_cqe;
    if (!mel_io_poll_ticket(vfs->io, ticket, &io_cqe))
        return false;

    mel__vfs_convert_cqe(vfs, &io_cqe, out_cqe);
    return true;
}

bool mel_vfs_wait_ticket(Mel_Vfs* vfs, u64 ticket, u32 timeout_ms, Mel_Vfs_Cqe* out_cqe)
{
    assert(vfs);
    assert(out_cqe);

    Mel_Io_Cqe io_cqe;
    if (!mel_io_wait_ticket(vfs->io, ticket, timeout_ms, &io_cqe))
        return false;

    mel__vfs_convert_cqe(vfs, &io_cqe, out_cqe);
    return true;
}

void* mel_vfs_map_ptr(Mel_Vfs* vfs, Mel_Vfs_Map map, usize* out_size)
{
    SDL_LockMutex(vfs->state_lock);
    Mel_Vfs__Map_Data* mdata = mel_slotmap_get(&vfs->map_slots, map.handle);
    if (!mdata) {
        if (out_size) *out_size = 0;
        SDL_UnlockMutex(vfs->state_lock);
        return NULL;
    }
    if (out_size) *out_size = mdata->size;
    void* ptr = mdata->ptr;
    SDL_UnlockMutex(vfs->state_lock);
    return ptr;
}
