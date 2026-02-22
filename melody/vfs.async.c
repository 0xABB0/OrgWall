#include "vfs.h"
#include "string.str8.h"
#include "string.path.h"

#include <string.h>

static usize mel__vfs_round_pow2(usize v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

static void mel__vfs_byte_ring_init(Mel_Vfs__Byte_Ring* r, const Mel_Alloc* alloc, usize capacity)
{
    r->capacity = mel__vfs_round_pow2(capacity);
    r->data = mel_alloc(alloc, r->capacity);
    r->head = 0;
    r->used = 0;
}

static void mel__vfs_byte_ring_free(Mel_Vfs__Byte_Ring* r, const Mel_Alloc* alloc)
{
    if (r->data) mel_dealloc(alloc, r->data);
    *r = (Mel_Vfs__Byte_Ring){0};
}

static u8* mel__vfs_byte_ring_alloc(Mel_Vfs__Byte_Ring* r, usize size)
{
    if (size == 0) return NULL;
    assert(size <= r->capacity - r->used);

    usize mask = r->capacity - 1;
    usize tail = (r->head + r->used) & mask;
    usize contig = r->capacity - tail;

    if (size <= contig) {
        r->used += size;
        return r->data + tail;
    }

    r->used += contig + size;
    return r->data;
}

static void mel__vfs_byte_ring_copy(Mel_Vfs__Byte_Ring* r, const u8* src, usize len, u8** out)
{
    u8* dst = mel__vfs_byte_ring_alloc(r, len);
    assert(dst);
    memcpy(dst, src, len);
    *out = dst;
}

static str8 mel__vfs_copy_path(Mel_Vfs* vfs, str8 path)
{
    if (path.len == 0) return (str8){0};
    u8* dst;
    mel__vfs_byte_ring_copy(&vfs->path_ring, path.data, (usize)path.len, &dst);
    return (str8){ .data = dst, .len = path.len };
}

static Mel_IoVec* mel__vfs_copy_iov(Mel_Vfs* vfs, const Mel_IoVec* iov, usize cnt)
{
    if (cnt == 0) return NULL;
    usize sz = sizeof(Mel_IoVec) * cnt;
    u8* dst = mel__vfs_byte_ring_alloc(&vfs->path_ring, sz);
    assert(dst);
    memcpy(dst, iov, sz);
    return (Mel_IoVec*)dst;
}

static bool mel__vfs_path_escapes_root(str8 normalized)
{
    return normalized.len == 0;
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

static void mel__vfs_execute_op(Mel_Vfs* vfs, Mel_Vfs_Sqe* sqe, Mel_Vfs_Cqe* cqe)
{
    cqe->ticket = sqe->ticket;
    cqe->op = sqe->op;
    cqe->status = MEL_VFS_STATUS_OK;
    cqe->user_data = sqe->user_data;

    u8 norm_buf[MEL_VFS_MAX_PATH];

    switch (sqe->op) {

    case MEL_VFS_OP_OPEN: {
        str8 np = mel_path_normalize(sqe->open.path, norm_buf, sizeof(norm_buf));
        if (mel__vfs_path_escapes_root(np)) {
            cqe->status = MEL_VFS_STATUS_PERMISSION;
            break;
        }

        i32 mi = mel__vfs_find_mount(vfs, np);
        if (mi < 0) {
            cqe->status = MEL_VFS_STATUS_NOT_FOUND;
            break;
        }

        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];

        if ((sqe->open.open_flags & MEL_VFS_OPEN_WRITE) && !mount->writable) {
            cqe->status = MEL_VFS_STATUS_PERMISSION;
            break;
        }

        str8 rel = mel__vfs_strip_prefix(np, mount->prefix);
        Mel_Vfs_Native_Handle nh;
        i32 err = mount->backend->open(mount->backend, rel, sqe->open.open_flags, &nh);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = err };
            break;
        }

        Mel_Vfs__File_Data fdata = {
            .backend = mount->backend,
            .native_handle = nh,
            .mount_generation = vfs->mount_generation,
            .mount_index = (u32)mi,
        };
        Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&vfs->file_slots, &fdata);
        mount->refcount++;

        cqe->file = (Mel_Vfs_File){ .handle = sm_handle };
    } break;

    case MEL_VFS_OP_CLOSE: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->close.file.handle);
        assert(fdata);
        fdata->backend->close(fdata->backend, fdata->native_handle);

        if (fdata->mount_index < (u32)vfs->mounts.count) {
            Mel_Vfs_Mount* mount = &vfs->mounts.items[fdata->mount_index];
            assert(mount->refcount > 0);
            mount->refcount--;
        }

        mel_slotmap_remove(&vfs->file_slots, sqe->close.file.handle);
    } break;

    case MEL_VFS_OP_READ: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->read.file.handle);
        assert(fdata);
        i64 n = fdata->backend->read(fdata->backend, fdata->native_handle,
                                     sqe->read.offset, sqe->read.dst, sqe->read.size);
        if (n < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = (i32)n };
        } else {
            cqe->result = n;
        }
    } break;

    case MEL_VFS_OP_WRITE: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->write.file.handle);
        assert(fdata);
        i64 n = fdata->backend->write(fdata->backend, fdata->native_handle,
                                      sqe->write.offset, sqe->write.src, sqe->write.size);
        if (n < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = (i32)n };
        } else {
            cqe->result = n;
        }
    } break;

    case MEL_VFS_OP_READV: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->readv.file.handle);
        assert(fdata);
        i64 n = fdata->backend->readv(fdata->backend, fdata->native_handle,
                                      sqe->readv.offset, sqe->readv.iov, sqe->readv.iov_cnt);
        if (n < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = (i32)n };
        } else {
            cqe->result = n;
        }
    } break;

    case MEL_VFS_OP_WRITEV: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->writev.file.handle);
        assert(fdata);
        i64 n = fdata->backend->writev(fdata->backend, fdata->native_handle,
                                       sqe->writev.offset, sqe->writev.iov, sqe->writev.iov_cnt);
        if (n < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = (i32)n };
        } else {
            cqe->result = n;
        }
    } break;

    case MEL_VFS_OP_MAP: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->map.file.handle);
        assert(fdata);
        void* ptr;
        i32 err = fdata->backend->map(fdata->backend, fdata->native_handle,
                                       sqe->map.offset, sqe->map.size, sqe->map.flags, &ptr);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = err };
        } else {
            Mel_Vfs__Map_Data mdata = {
                .ptr = ptr,
                .size = sqe->map.size,
                .backend = fdata->backend,
                .file_native_handle = fdata->native_handle,
            };
            Mel_SlotMap_Handle mh = mel_slotmap_insert(&vfs->map_slots, &mdata);
            cqe->map = (Mel_Vfs_Map){ .handle = mh };
        }
    } break;

    case MEL_VFS_OP_UNMAP: {
        Mel_Vfs__Map_Data* mdata = mel_slotmap_get(&vfs->map_slots, sqe->unmap.map.handle);
        assert(mdata);
        mdata->backend->unmap(mdata->backend, mdata->ptr, mdata->size);
        mel_slotmap_remove(&vfs->map_slots, sqe->unmap.map.handle);
    } break;

    case MEL_VFS_OP_STAT: {
        str8 np = mel_path_normalize(sqe->stat.path, norm_buf, sizeof(norm_buf));
        if (mel__vfs_path_escapes_root(np)) {
            cqe->status = MEL_VFS_STATUS_PERMISSION;
            break;
        }

        i32 mi = mel__vfs_find_mount(vfs, np);
        if (mi < 0) {
            cqe->status = MEL_VFS_STATUS_NOT_FOUND;
            break;
        }

        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        str8 rel = mel__vfs_strip_prefix(np, mount->prefix);
        Mel_Vfs_Stat st;
        i32 err = mount->backend->stat(mount->backend, rel, &st);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_NOT_FOUND;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = err };
        } else {
            cqe->stat = st;
        }
    } break;

    case MEL_VFS_OP_DIR_OPEN: {
        str8 np = mel_path_normalize(sqe->dir_open.path, norm_buf, sizeof(norm_buf));
        if (mel__vfs_path_escapes_root(np)) {
            cqe->status = MEL_VFS_STATUS_PERMISSION;
            break;
        }

        i32 mi = mel__vfs_find_mount(vfs, np);
        if (mi < 0) {
            cqe->status = MEL_VFS_STATUS_NOT_FOUND;
            break;
        }

        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        str8 rel = mel__vfs_strip_prefix(np, mount->prefix);
        Mel_Vfs_Native_Handle nh;
        i32 err = mount->backend->dir_open(mount->backend, rel, &nh);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = err };
            break;
        }

        Mel_Vfs__Dir_Data ddata = {
            .backend = mount->backend,
            .native_handle = nh,
        };
        Mel_SlotMap_Handle dh = mel_slotmap_insert(&vfs->dir_slots, &ddata);
        cqe->dir = (Mel_Vfs_Dir){ .handle = dh };
    } break;

    case MEL_VFS_OP_DIR_NEXT: {
        Mel_Vfs__Dir_Data* ddata = mel_slotmap_get(&vfs->dir_slots, sqe->dir_next.dir.handle);
        assert(ddata);
        usize name_len = 0;
        Mel_Vfs_Stat entry_stat;
        i32 err = ddata->backend->dir_next(ddata->backend, ddata->native_handle,
                                            sqe->dir_next.name_buf, sqe->dir_next.name_cap,
                                            &name_len, &entry_stat);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_BUFFER_TOO_SMALL;
            cqe->result = (i64)name_len;
        } else if (err == 1) {
            cqe->status = MEL_VFS_STATUS_EOF;
        } else {
            sqe->dir_next.entry->name = (str8){ .data = sqe->dir_next.name_buf, .len = (size)name_len };
            sqe->dir_next.entry->stat = entry_stat;
            cqe->result = 1;
        }
    } break;

    case MEL_VFS_OP_DIR_NEXT_BATCH: {
        Mel_Vfs__Dir_Data* ddata = mel_slotmap_get(&vfs->dir_slots, sqe->dir_next_batch.dir.handle);
        assert(ddata);
        usize filled = 0;
        usize blob_offset = 0;
        u8* blob = (u8*)sqe->dir_next_batch.str_blob;
        bool eof = false;

        while (filled < sqe->dir_next_batch.entry_cap && !eof) {
            usize remaining = sqe->dir_next_batch.str_blob_cap - blob_offset;
            usize name_len = 0;
            Mel_Vfs_Stat entry_stat;
            i32 err = ddata->backend->dir_next(ddata->backend, ddata->native_handle,
                                                blob + blob_offset, remaining,
                                                &name_len, &entry_stat);
            if (err < 0) {
                if (filled == 0) {
                    cqe->status = MEL_VFS_STATUS_BUFFER_TOO_SMALL;
                    cqe->result = (i64)name_len;
                }
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

    case MEL_VFS_OP_DIR_CLOSE: {
        Mel_Vfs__Dir_Data* ddata = mel_slotmap_get(&vfs->dir_slots, sqe->dir_close.dir.handle);
        assert(ddata);
        ddata->backend->dir_close(ddata->backend, ddata->native_handle);
        mel_slotmap_remove(&vfs->dir_slots, sqe->dir_close.dir.handle);
    } break;

    case MEL_VFS_OP_WATCH_OPEN: {
        str8 np = mel_path_normalize(sqe->watch_open.path, norm_buf, sizeof(norm_buf));
        if (mel__vfs_path_escapes_root(np)) {
            cqe->status = MEL_VFS_STATUS_PERMISSION;
            break;
        }

        i32 mi = mel__vfs_find_mount(vfs, np);
        if (mi < 0) {
            cqe->status = MEL_VFS_STATUS_NOT_FOUND;
            break;
        }

        Mel_Vfs_Mount* mount = &vfs->mounts.items[mi];
        if (!mount->backend->watch_open) {
            cqe->status = MEL_VFS_STATUS_UNSUPPORTED;
            break;
        }

        str8 rel = mel__vfs_strip_prefix(np, mount->prefix);
        Mel_Vfs_Native_Handle nh;
        i32 err = mount->backend->watch_open(mount->backend, rel,
                                              sqe->watch_open.recursive,
                                              sqe->watch_open.flags, &nh);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = err };
            break;
        }

        Mel_Vfs__Watch_Data wdata = {
            .backend = mount->backend,
            .native_handle = nh,
        };
        Mel_SlotMap_Handle wh = mel_slotmap_insert(&vfs->watch_slots, &wdata);
        cqe->watch = (Mel_Vfs_Watch){ .handle = wh };
    } break;

    case MEL_VFS_OP_WATCH_NEXT: {
        Mel_Vfs__Watch_Data* wdata = mel_slotmap_get(&vfs->watch_slots, sqe->watch_next.watch.handle);
        assert(wdata);
        if (!wdata->backend->watch_next) {
            cqe->status = MEL_VFS_STATUS_UNSUPPORTED;
            break;
        }

        usize path_len = 0;
        i32 action = 0;
        i32 err = wdata->backend->watch_next(wdata->backend, wdata->native_handle,
                                              -1,
                                              sqe->watch_next.path_buf,
                                              sqe->watch_next.path_cap,
                                              &path_len, &action);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_BUFFER_TOO_SMALL;
            cqe->result = (i64)path_len;
        } else if (err == 1) {
            cqe->status = MEL_VFS_STATUS_TIMEOUT;
        } else {
            cqe->result = action;
            cqe->path_str = (str8){ .data = sqe->watch_next.path_buf, .len = (size)path_len };
        }
    } break;

    case MEL_VFS_OP_WATCH_CLOSE: {
        Mel_Vfs__Watch_Data* wdata = mel_slotmap_get(&vfs->watch_slots, sqe->watch_close.watch.handle);
        assert(wdata);
        if (wdata->backend->watch_close)
            wdata->backend->watch_close(wdata->backend, wdata->native_handle);
        mel_slotmap_remove(&vfs->watch_slots, sqe->watch_close.watch.handle);
    } break;

    case MEL_VFS_OP_SYNC: {
        Mel_Vfs__File_Data* fdata = mel_slotmap_get(&vfs->file_slots, sqe->sync.file.handle);
        assert(fdata);
        i32 err = fdata->backend->sync(fdata->backend, fdata->native_handle);
        if (err < 0) {
            cqe->status = MEL_VFS_STATUS_IO_ERROR;
            cqe->error = (Mel_Vfs_Error){ .category = MEL_VFS_ERRCAT_BACKEND, .code = err };
        }
    } break;

    case MEL_VFS_OP_CANCEL: {
        cqe->status = MEL_VFS_STATUS_UNSUPPORTED;
    } break;

    default: {
        cqe->status = MEL_VFS_STATUS_INVALID_ARGUMENT;
    } break;

    }
}

static void mel__vfs_io_handler(void* ctx, const Mel_Io_Sqe* io_sqe, Mel_Io_Cqe* io_cqe)
{
    Mel_Vfs* vfs = (Mel_Vfs*)ctx;
    Mel_Vfs__Op* op = (Mel_Vfs__Op*)io_sqe->op_data;

    memset(&op->cqe, 0, sizeof(op->cqe));
    mel__vfs_execute_op(vfs, &op->sqe, &op->cqe);

    io_cqe->status = (op->cqe.status == MEL_VFS_STATUS_OK) ? MEL_IO_STATUS_OK : MEL_IO_STATUS_ERROR;
    io_cqe->result = op->cqe.result;
    io_cqe->result_data = &op->cqe;
}

static void mel__vfs_convert_cqe(const Mel_Io_Cqe* io_cqe, Mel_Vfs_Cqe* out)
{
    if (io_cqe->result_data) {
        *out = *(Mel_Vfs_Cqe*)io_cqe->result_data;
    } else {
        *out = (Mel_Vfs_Cqe){
            .ticket = io_cqe->ticket,
            .status = MEL_VFS_STATUS_CANCELLED,
            .user_data = io_cqe->user_data,
        };
    }
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

    mel__vfs_byte_ring_init(&vfs->path_ring, vfs->alloc, MEL_VFS_RING_BUFFER_SIZE);

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

    mel__vfs_byte_ring_free(&vfs->path_ring, vfs->alloc);
}

u64 mel_vfs_next_ticket(Mel_Vfs* vfs)
{
    return mel_io_next_ticket(vfs->io);
}

i32 mel_vfs_submit(Mel_Vfs* vfs, const Mel_Vfs_Sqe* sqes, i32 count)
{
    assert(vfs);
    assert(sqes || count == 0);

    usize io_sqes_size = sizeof(Mel_Io_Sqe) * (usize)count;
    Mel_Io_Sqe* io_sqes = (Mel_Io_Sqe*)mel__vfs_byte_ring_alloc(&vfs->path_ring, io_sqes_size);
    memset(io_sqes, 0, io_sqes_size);

    for (i32 i = 0; i < count; i++) {
        assert(sqes[i].ticket != 0);

        Mel_Vfs__Op* op = (Mel_Vfs__Op*)mel__vfs_byte_ring_alloc(&vfs->path_ring, sizeof(Mel_Vfs__Op));
        memset(op, 0, sizeof(*op));
        op->sqe = sqes[i];

        switch (op->sqe.op) {
        case MEL_VFS_OP_OPEN:
            op->sqe.open.path = mel__vfs_copy_path(vfs, op->sqe.open.path);
            break;
        case MEL_VFS_OP_STAT:
            op->sqe.stat.path = mel__vfs_copy_path(vfs, op->sqe.stat.path);
            break;
        case MEL_VFS_OP_DIR_OPEN:
            op->sqe.dir_open.path = mel__vfs_copy_path(vfs, op->sqe.dir_open.path);
            break;
        case MEL_VFS_OP_WATCH_OPEN:
            op->sqe.watch_open.path = mel__vfs_copy_path(vfs, op->sqe.watch_open.path);
            break;
        case MEL_VFS_OP_READV:
            op->sqe.readv.iov = mel__vfs_copy_iov(vfs, op->sqe.readv.iov, op->sqe.readv.iov_cnt);
            break;
        case MEL_VFS_OP_WRITEV:
            op->sqe.writev.iov = (Mel_IoVec*)mel__vfs_copy_iov(vfs, op->sqe.writev.iov, op->sqe.writev.iov_cnt);
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

    return mel_io_submit(vfs->io, io_sqes, count);
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
            mel__vfs_convert_cqe(&io_cqes[i], &out_cqes[total + i]);
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

    mel__vfs_convert_cqe(&io_cqe, out_cqe);
    return true;
}

bool mel_vfs_wait_ticket(Mel_Vfs* vfs, u64 ticket, u32 timeout_ms, Mel_Vfs_Cqe* out_cqe)
{
    assert(vfs);
    assert(out_cqe);

    Mel_Io_Cqe io_cqe;
    if (!mel_io_wait_ticket(vfs->io, ticket, timeout_ms, &io_cqe))
        return false;

    mel__vfs_convert_cqe(&io_cqe, out_cqe);
    return true;
}

void* mel_vfs_map_ptr(Mel_Vfs* vfs, Mel_Vfs_Map map, usize* out_size)
{
    Mel_Vfs__Map_Data* mdata = mel_slotmap_get(&vfs->map_slots, map.handle);
    assert(mdata);
    if (out_size) *out_size = mdata->size;
    return mdata->ptr;
}
