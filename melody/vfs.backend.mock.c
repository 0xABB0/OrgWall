#include "vfs.backend.mock.h"
#include "vfs.async.h"
#include "allocator.h"
#include "string.str8.h"
#include "collection.array.h"

#include <string.h>

typedef struct {
    u8*   path_data;
    size  path_len;
    u8*   data;
    usize size;
    usize capacity;
    u32   flags;
} Mel_Vfs__Mock_Entry;

typedef struct {
    usize entry_index;
} Mel_Vfs__Mock_Open_File;

typedef struct {
    str8  dir_path;
    usize cursor;
} Mel_Vfs__Mock_Open_Dir;

typedef struct {
    const Mel_Alloc* alloc;
    Mel_Array(Mel_Vfs__Mock_Entry) entries;
    Mel_Array(Mel_Vfs__Mock_Open_File) open_files;
    Mel_Array(Mel_Vfs__Mock_Open_Dir) open_dirs;
} Mel_Vfs__Mock_Data;

static bool mel__mock_path_eq(Mel_Vfs__Mock_Entry* e, str8 path)
{
    if (e->path_len != path.len) return false;
    return memcmp(e->path_data, path.data, (usize)path.len) == 0;
}

static i32 mel__mock_find_entry(Mel_Vfs__Mock_Data* d, str8 path)
{
    for (usize i = 0; i < d->entries.count; i++) {
        if (mel__mock_path_eq(&d->entries.items[i], path)) return (i32)i;
    }
    return -1;
}

static bool mel__mock_is_child_of(Mel_Vfs__Mock_Entry* e, str8 dir_path)
{
    if (dir_path.len == 1 && dir_path.data[0] == '.') {
        for (size i = 0; i < e->path_len; i++) {
            if (e->path_data[i] == '/') return false;
        }
        return true;
    }

    if (e->path_len <= dir_path.len) return false;
    if (memcmp(e->path_data, dir_path.data, (usize)dir_path.len) != 0) return false;
    if (e->path_data[dir_path.len] != '/') return false;

    for (size i = dir_path.len + 1; i < e->path_len; i++) {
        if (e->path_data[i] == '/') return false;
    }
    return true;
}

static str8 mel__mock_child_name(Mel_Vfs__Mock_Entry* e, str8 dir_path)
{
    if (dir_path.len == 1 && dir_path.data[0] == '.') {
        return (str8){ .data = e->path_data, .len = e->path_len };
    }
    return (str8){
        .data = e->path_data + dir_path.len + 1,
        .len = e->path_len - dir_path.len - 1,
    };
}

static i32 mel__mock_open(Mel_Vfs_Backend* b, str8 path, u32 flags, Mel_Vfs_Native_Handle* out)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;

    i32 idx = mel__mock_find_entry(d, path);

    if (idx < 0) {
        if (!(flags & MEL_VFS_OPEN_CREATE)) return -2;

        Mel_Vfs__Mock_Entry entry = {0};
        entry.path_data = mel_alloc(d->alloc, (usize)path.len);
        memcpy(entry.path_data, path.data, (usize)path.len);
        entry.path_len = path.len;
        entry.flags = MEL_VFS_STAT_IS_FILE;
        mel_array_push(&d->entries, entry);
        idx = (i32)(d->entries.count - 1);
    }

    if (flags & MEL_VFS_OPEN_TRUNCATE) {
        d->entries.items[idx].size = 0;
    }

    Mel_Vfs__Mock_Open_File of = { .entry_index = (usize)idx };
    mel_array_push(&d->open_files, of);
    *out = (Mel_Vfs_Native_Handle)d->open_files.count;
    return 0;
}

static void mel__mock_close(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
}

static i64 mel__mock_read(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, void* dst, usize size)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;
    Mel_Vfs__Mock_Open_File* of = &d->open_files.items[h - 1];
    Mel_Vfs__Mock_Entry* e = &d->entries.items[of->entry_index];

    if (offset >= e->size) return 0;

    usize avail = e->size - (usize)offset;
    usize to_read = (size < avail) ? size : avail;
    memcpy(dst, e->data + offset, to_read);
    return (i64)to_read;
}

static i64 mel__mock_write(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, const void* src, usize size)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;
    Mel_Vfs__Mock_Open_File* of = &d->open_files.items[h - 1];
    Mel_Vfs__Mock_Entry* e = &d->entries.items[of->entry_index];

    usize end = (usize)offset + size;
    if (end > e->capacity) {
        usize new_cap = e->capacity ? e->capacity : 256;
        while (new_cap < end) new_cap *= 2;
        u8* new_data = mel_alloc(d->alloc, new_cap);
        if (e->data) {
            memcpy(new_data, e->data, e->size);
            mel_dealloc(d->alloc, e->data);
        }
        e->data = new_data;
        e->capacity = new_cap;
    }

    memcpy(e->data + offset, src, size);
    if (end > e->size) e->size = end;
    return (i64)size;
}

static i64 mel__mock_readv(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, Mel_IoVec* iov, usize iov_cnt)
{
    i64 total = 0;
    for (usize i = 0; i < iov_cnt; i++) {
        i64 n = mel__mock_read(b, h, offset + (u64)total, iov[i].buffer, iov[i].len);
        if (n < 0) return n;
        total += n;
        if ((usize)n < iov[i].len) break;
    }
    return total;
}

static i64 mel__mock_writev(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, const Mel_IoVec* iov, usize iov_cnt)
{
    i64 total = 0;
    for (usize i = 0; i < iov_cnt; i++) {
        i64 n = mel__mock_write(b, h, offset + (u64)total, iov[i].buffer, iov[i].len);
        if (n < 0) return n;
        total += n;
    }
    return total;
}

static i32 mel__mock_stat(Mel_Vfs_Backend* b, str8 path, Mel_Vfs_Stat* out)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;
    i32 idx = mel__mock_find_entry(d, path);
    if (idx < 0) return -2;

    Mel_Vfs__Mock_Entry* e = &d->entries.items[idx];
    *out = (Mel_Vfs_Stat){
        .size = (u64)e->size,
        .flags = e->flags,
    };
    return 0;
}

static i32 mel__mock_dir_open(Mel_Vfs_Backend* b, str8 path, Mel_Vfs_Native_Handle* out)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;

    u8* path_copy = NULL;
    if (path.len > 0) {
        path_copy = mel_alloc(d->alloc, (usize)path.len);
        memcpy(path_copy, path.data, (usize)path.len);
    }

    Mel_Vfs__Mock_Open_Dir od = {
        .dir_path = { .data = path_copy, .len = path.len },
        .cursor = 0,
    };
    mel_array_push(&d->open_dirs, od);
    *out = (Mel_Vfs_Native_Handle)d->open_dirs.count;
    return 0;
}

static i32 mel__mock_dir_next(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h,
                               u8* name_buf, usize name_cap, usize* out_name_len,
                               Mel_Vfs_Stat* out_stat)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;
    Mel_Vfs__Mock_Open_Dir* od = &d->open_dirs.items[h - 1];

    while (od->cursor < d->entries.count) {
        Mel_Vfs__Mock_Entry* e = &d->entries.items[od->cursor];
        od->cursor++;

        if (!mel__mock_is_child_of(e, od->dir_path)) continue;

        str8 name = mel__mock_child_name(e, od->dir_path);
        *out_name_len = (usize)name.len;
        if ((usize)name.len > name_cap) return -1;

        memcpy(name_buf, name.data, (usize)name.len);
        *out_stat = (Mel_Vfs_Stat){
            .size = (u64)e->size,
            .flags = e->flags,
        };
        return 0;
    }

    return 1;
}

static void mel__mock_dir_close(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;
    Mel_Vfs__Mock_Open_Dir* od = &d->open_dirs.items[h - 1];
    if (od->dir_path.data) {
        mel_dealloc(d->alloc, od->dir_path.data);
        od->dir_path = (str8){0};
    }
}

static i32 mel__mock_map(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h, u64 offset, usize size, u32 flags, void** out_ptr)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;
    Mel_Vfs__Mock_Open_File* of = &d->open_files.items[h - 1];
    Mel_Vfs__Mock_Entry* e = &d->entries.items[of->entry_index];
    MEL_UNUSED(flags);

    if (offset + size > e->size) return -1;
    *out_ptr = e->data + offset;
    return 0;
}

static void mel__mock_unmap(Mel_Vfs_Backend* b, void* ptr, usize size)
{
    MEL_UNUSED(b);
    MEL_UNUSED(ptr);
    MEL_UNUSED(size);
}

static i32 mel__mock_sync(Mel_Vfs_Backend* b, Mel_Vfs_Native_Handle h)
{
    MEL_UNUSED(b);
    MEL_UNUSED(h);
    return 0;
}

static void mel__mock_destroy(Mel_Vfs_Backend* b)
{
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)b->impl_data;
    const Mel_Alloc* alloc = d->alloc;

    for (usize i = 0; i < d->entries.count; i++) {
        Mel_Vfs__Mock_Entry* e = &d->entries.items[i];
        if (e->path_data) mel_dealloc(alloc, e->path_data);
        if (e->data) mel_dealloc(alloc, e->data);
    }
    for (usize i = 0; i < d->open_dirs.count; i++) {
        Mel_Vfs__Mock_Open_Dir* od = &d->open_dirs.items[i];
        if (od->dir_path.data) mel_dealloc(alloc, od->dir_path.data);
    }
    mel_array_free(&d->entries);
    mel_array_free(&d->open_files);
    mel_array_free(&d->open_dirs);
    mel_dealloc(alloc, d);
    mel_dealloc(alloc, b);
}

Mel_Vfs_Backend* mel_vfs_backend_mock_create(const Mel_Alloc* alloc)
{
    assert(alloc);

    Mel_Vfs_Backend* b = mel_calloc(alloc, sizeof(Mel_Vfs_Backend));
    Mel_Vfs__Mock_Data* d = mel_calloc(alloc, sizeof(Mel_Vfs__Mock_Data));

    d->alloc = alloc;
    mel_array_init(&d->entries, alloc);
    mel_array_init(&d->open_files, alloc);
    mel_array_init(&d->open_dirs, alloc);

    b->open       = mel__mock_open;
    b->close      = mel__mock_close;
    b->read       = mel__mock_read;
    b->write      = mel__mock_write;
    b->readv      = mel__mock_readv;
    b->writev     = mel__mock_writev;
    b->stat       = mel__mock_stat;
    b->dir_open   = mel__mock_dir_open;
    b->dir_next   = mel__mock_dir_next;
    b->dir_close  = mel__mock_dir_close;
    b->map        = mel__mock_map;
    b->unmap      = mel__mock_unmap;
    b->watch_open  = NULL;
    b->watch_next  = NULL;
    b->watch_close = NULL;
    b->sync       = mel__mock_sync;
    b->destroy    = mel__mock_destroy;
    b->impl_data  = d;

    return b;
}

void mel_vfs_backend_mock_destroy(Mel_Vfs_Backend* backend)
{
    assert(backend);
    backend->destroy(backend);
}

void mel_vfs_backend_mock_inject_file(Mel_Vfs_Backend* backend, str8 path, const u8* data, usize size)
{
    assert(backend);
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)backend->impl_data;

    i32 idx = mel__mock_find_entry(d, path);
    if (idx >= 0) {
        Mel_Vfs__Mock_Entry* e = &d->entries.items[idx];
        if (e->data) mel_dealloc(d->alloc, e->data);
        e->data = mel_alloc(d->alloc, size);
        memcpy(e->data, data, size);
        e->size = size;
        e->capacity = size;
        e->flags = MEL_VFS_STAT_IS_FILE;
        return;
    }

    Mel_Vfs__Mock_Entry entry = {0};
    entry.path_data = mel_alloc(d->alloc, (usize)path.len);
    memcpy(entry.path_data, path.data, (usize)path.len);
    entry.path_len = path.len;
    entry.flags = MEL_VFS_STAT_IS_FILE;

    if (size > 0) {
        entry.data = mel_alloc(d->alloc, size);
        memcpy(entry.data, data, size);
        entry.size = size;
        entry.capacity = size;
    }

    mel_array_push(&d->entries, entry);
}

void mel_vfs_backend_mock_inject_dir(Mel_Vfs_Backend* backend, str8 path)
{
    assert(backend);
    Mel_Vfs__Mock_Data* d = (Mel_Vfs__Mock_Data*)backend->impl_data;

    i32 idx = mel__mock_find_entry(d, path);
    if (idx >= 0) {
        d->entries.items[idx].flags = MEL_VFS_STAT_IS_DIR;
        return;
    }

    Mel_Vfs__Mock_Entry entry = {0};
    entry.path_data = mel_alloc(d->alloc, (usize)path.len);
    memcpy(entry.path_data, path.data, (usize)path.len);
    entry.path_len = path.len;
    entry.flags = MEL_VFS_STAT_IS_DIR;

    mel_array_push(&d->entries, entry);
}
