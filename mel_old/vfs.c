#include "vfs.h"
#include "str8.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "async.aio.h"
#include "async.signal.h"
#include "async.job.h"

#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <SDL3/SDL.h>

typedef struct {
    str8 prefix;
    str8 root;
    Mel_Vfs_Backend* backend;
    u8 priority;
    bool writable;
    bool active;
} Mel__Vfs_Mount;

typedef struct {
    Mel_Vfs_Backend* backend;
    i32 backend_handle;
    u32 generation;
    bool active;
} Mel__Vfs_File_Slot;

static struct {
    Mel__Vfs_Mount mounts[MEL_VFS_MAX_MOUNTS];
    u32 mount_count;

    Mel__Vfs_File_Slot files[MEL_VFS_MAX_OPEN_FILES];
    u32 generation_counter;

    const Mel_Alloc* alloc;
    bool initialized;
} s_vfs;

static size mel__vfs_normalize(const u8* src, size src_len, u8* dst, size dst_cap)
{
    if (src_len <= 0) { dst[0] = '/'; return 1; }

    size out = 0;

    for (size i = 0; i < src_len && out < dst_cap - 1; )
    {
        u8 c = src[i];
        if (c == '\\') c = '/';

        if (c == '/')
        {
            if (out > 0 && dst[out - 1] == '/') { i++; continue; }

            if (i + 1 < src_len && src[i + 1] == '.')
            {
                if (i + 2 >= src_len || src[i + 2] == '/' || src[i + 2] == '\\')
                {
                    i += 2;
                    continue;
                }
                if (i + 2 < src_len && src[i + 2] == '.' &&
                    (i + 3 >= src_len || src[i + 3] == '/' || src[i + 3] == '\\'))
                {
                    if (out > 1)
                    {
                        out--;
                        while (out > 1 && dst[out - 1] != '/') out--;
                    }
                    i += 3;
                    continue;
                }
            }
        }

        dst[out++] = c;
        i++;
    }

    if (out > 1 && dst[out - 1] == '/') out--;
    if (out == 0) { dst[0] = '/'; out = 1; }
    return out;
}

static str8 mel__vfs_normalize_alloc(str8 path, const Mel_Alloc* alloc)
{
    u8 buf[MEL_VFS_PATH_MAX];
    size len = mel__vfs_normalize(path.data, path.len, buf, MEL_VFS_PATH_MAX);
    u8* dup = mel_alloc(alloc, (usize)(len + 1));
    memcpy(dup, buf, (size_t)len);
    dup[len] = 0;
    return str8_from_parts(dup, len);
}

static str8 mel__vfs_normalize_stack(str8 path, u8* buf, size cap)
{
    size len = mel__vfs_normalize(path.data, path.len, buf, cap);
    return str8_from_parts(buf, len);
}

static bool mel__vfs_starts_with(str8 path, str8 prefix)
{
    if (prefix.len > path.len) return false;
    if (prefix.len == 1 && prefix.data[0] == '/') return true;
    if (memcmp(path.data, prefix.data, (size_t)prefix.len) != 0) return false;
    return prefix.len == path.len || path.data[prefix.len] == '/';
}

static str8 mel__vfs_strip_prefix(str8 path, str8 prefix)
{
    if (prefix.len == 1 && prefix.data[0] == '/') return path;
    if (prefix.len >= path.len) return S8("/");
    str8 result = str8_from_parts(path.data + prefix.len, path.len - prefix.len);
    if (result.len == 0 || result.data[0] != '/')
    {
        static u8 slash = '/';
        (void)slash;
        return S8("/");
    }
    return result;
}

static str8 mel__vfs_resolve_physical(str8 stripped, str8 root, u8* buf, size cap)
{
    size out = 0;

    if (root.len > 0 && !(root.len == 1 && root.data[0] == '/'))
    {
        size copy = root.len < cap - 1 ? root.len : cap - 1;
        memcpy(buf, root.data, (size_t)copy);
        out = copy;
    }

    if (stripped.len > 0)
    {
        size copy = stripped.len < cap - 1 - out ? stripped.len : cap - 1 - out;
        memcpy(buf + out, stripped.data, (size_t)copy);
        out += copy;
    }

    if (out == 0) { buf[0] = '/'; out = 1; }
    buf[out] = 0;
    return str8_from_parts(buf, out);
}

static void mel__vfs_drain_wait(Mel_Counter* counter)
{
    if (mel_job_is_worker_fiber())
    {
        while (mel__signal_counter(atomic_load_explicit(&counter->signal.state, memory_order_acquire)) != 0)
        {
            mel_aio_drain();
            mel_job_yield();
        }
    }
    else
    {
        while (mel__signal_counter(atomic_load_explicit(&counter->signal.state, memory_order_acquire)) != 0)
            mel_aio_drain();
    }
}

static Mel__Vfs_Mount* mel__vfs_find_mount(str8 normalized_path, bool need_write)
{
    Mel__Vfs_Mount* best = NULL;

    for (u32 i = 0; i < s_vfs.mount_count; i++)
    {
        Mel__Vfs_Mount* m = &s_vfs.mounts[i];
        if (!m->active) continue;
        if (need_write && !m->writable) continue;
        if (!mel__vfs_starts_with(normalized_path, m->prefix)) continue;

        if (!best ||
            m->prefix.len > best->prefix.len ||
            (m->prefix.len == best->prefix.len && m->priority > best->priority))
        {
            best = m;
        }
    }

    return best;
}

__attribute__((constructor(280)))
void mel_vfs_init_default(void)
{
    mel_vfs_init(mel_alloc_heap());
}

void mel_vfs_init(const Mel_Alloc* alloc)
{
    if (s_vfs.initialized) return;
    assert(alloc != NULL);

    memset(&s_vfs, 0, sizeof(s_vfs));
    s_vfs.alloc = alloc;
    s_vfs.generation_counter = 1;
    s_vfs.initialized = true;
}

__attribute__((destructor(280)))
void mel_vfs_shutdown(void)
{
    if (!s_vfs.initialized) return;

    for (u32 i = 0; i < MEL_VFS_MAX_OPEN_FILES; i++)
    {
        if (s_vfs.files[i].active)
        {
            assert(s_vfs.files[i].backend);
            s_vfs.files[i].backend->close(s_vfs.files[i].backend, s_vfs.files[i].backend_handle);
            s_vfs.files[i].active = false;
        }
    }

    for (u32 i = 0; i < s_vfs.mount_count; i++)
    {
        if (s_vfs.mounts[i].active)
        {
            if (s_vfs.mounts[i].prefix.data)
                mel_dealloc(s_vfs.alloc, s_vfs.mounts[i].prefix.data);
            if (s_vfs.mounts[i].root.data)
                mel_dealloc(s_vfs.alloc, s_vfs.mounts[i].root.data);
            s_vfs.mounts[i].active = false;
        }
    }

    s_vfs.mount_count = 0;
    s_vfs.initialized = false;
}

void mel_vfs_mount_opt(str8 prefix, Mel_Vfs_Backend* backend, Mel_Vfs_Mount_Opt opt)
{
    assert(s_vfs.initialized);
    assert(backend != NULL);
    assert(s_vfs.mount_count < MEL_VFS_MAX_MOUNTS);

    str8 norm_prefix = mel__vfs_normalize_alloc(prefix, s_vfs.alloc);

    str8 norm_root = {0};
    if (opt.root.len > 0)
    {
        u8 buf[MEL_VFS_PATH_MAX];
        size len = mel__vfs_normalize(opt.root.data, opt.root.len, buf, MEL_VFS_PATH_MAX);
        u8* dup = mel_alloc(s_vfs.alloc, (usize)(len + 1));
        memcpy(dup, buf, (size_t)len);
        dup[len] = 0;
        norm_root = str8_from_parts(dup, len);
    }

    u32 slot = s_vfs.mount_count;
    for (u32 i = 0; i < s_vfs.mount_count; i++)
    {
        if (!s_vfs.mounts[i].active) { slot = i; break; }
    }
    if (slot == s_vfs.mount_count) s_vfs.mount_count++;

    s_vfs.mounts[slot] = (Mel__Vfs_Mount){
        .prefix = norm_prefix,
        .root = norm_root,
        .backend = backend,
        .priority = opt.priority,
        .writable = opt.writable,
        .active = true,
    };
}

void mel_vfs_unmount(str8 prefix)
{
    assert(s_vfs.initialized);

    u8 buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(prefix, buf, MEL_VFS_PATH_MAX);

    for (u32 i = 0; i < s_vfs.mount_count; i++)
    {
        if (!s_vfs.mounts[i].active) continue;
        if (str8_equals(s_vfs.mounts[i].prefix, norm))
        {
            if (s_vfs.mounts[i].prefix.data)
                mel_dealloc(s_vfs.alloc, s_vfs.mounts[i].prefix.data);
            if (s_vfs.mounts[i].root.data)
                mel_dealloc(s_vfs.alloc, s_vfs.mounts[i].root.data);
            s_vfs.mounts[i].active = false;
            return;
        }
    }
}

u8* mel_vfs_read_file(str8 path, i64* out_size, const Mel_Alloc* alloc)
{
    assert(s_vfs.initialized);
    assert(alloc != NULL);

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, false);
    if (!mount) return NULL;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    i32 fd = mount->backend->open(mount->backend, phys, MEL_VFS_OPEN_READ);
    if (fd < 0) return NULL;

    i64 length = mount->backend->file_length(mount->backend, fd);
    if (length < 0)
    {
        mount->backend->close(mount->backend, fd);
        return NULL;
    }

    u8* data = mel_alloc(alloc, (usize)(length + 1));

    i64 read_bytes;
    if (mount->backend->caps & MEL_VFS_CAP_ASYNC)
    {
        i64 result = 0;
        i32 err = 0;
        Mel_Counter counter = MEL_COUNTER_INIT;
        mel_counter_increment(&counter);

        Mel_Aio_Op op = {
            .fd = fd,
            .buf = data,
            .size = length,
            .offset = 0,
            .counter = &counter,
            .result = &result,
            .error = &err,
        };

        mel_aio_submit(&op);
        mel__vfs_drain_wait(&counter);
        read_bytes = err == 0 ? result : -1;
    }
    else
    {
        read_bytes = mount->backend->read(mount->backend, fd, data, length, 0);
    }

    mount->backend->close(mount->backend, fd);

    if (read_bytes != length)
    {
        mel_dealloc(alloc, data);
        return NULL;
    }

    data[length] = 0;
    if (out_size) *out_size = length;
    return data;
}

i64 mel_vfs_read(str8 path, void* buf, i64 size, i64 offset)
{
    assert(s_vfs.initialized);

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, false);
    if (!mount) return -1;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    i32 fd = mount->backend->open(mount->backend, phys, MEL_VFS_OPEN_READ);
    if (fd < 0) return -1;

    i64 read_result;
    if (mount->backend->caps & MEL_VFS_CAP_ASYNC)
    {
        i64 aio_result = 0;
        i32 aio_err = 0;
        Mel_Counter counter = MEL_COUNTER_INIT;
        mel_counter_increment(&counter);

        Mel_Aio_Op op = {
            .fd = fd,
            .buf = buf,
            .size = size,
            .offset = offset,
            .counter = &counter,
            .result = &aio_result,
            .error = &aio_err,
        };

        mel_aio_submit(&op);
        mel__vfs_drain_wait(&counter);
        read_result = aio_err == 0 ? aio_result : -1;
    }
    else
    {
        read_result = mount->backend->read(mount->backend, fd, buf, size, offset);
    }

    mount->backend->close(mount->backend, fd);
    return read_result;
}

bool mel_vfs_write_file(str8 path, const void* data, i64 size)
{
    assert(s_vfs.initialized);

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, true);
    if (!mount) return false;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    i32 fd = mount->backend->open(mount->backend, phys,
        MEL_VFS_OPEN_WRITE | MEL_VFS_OPEN_CREATE | MEL_VFS_OPEN_TRUNCATE);
    if (fd < 0) return false;

    i64 written = mount->backend->write(mount->backend, fd, data, size, 0);
    mount->backend->close(mount->backend, fd);
    return written == size;
}

bool mel_vfs_stat(str8 path, Mel_Vfs_Stat* out)
{
    assert(s_vfs.initialized);

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, false);
    if (!mount) return false;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    return mount->backend->stat(mount->backend, phys, out);
}

void mel_vfs_enumerate(str8 path, Mel_Vfs_Enumerate_Cb cb, void* user)
{
    assert(s_vfs.initialized);

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, false);
    if (!mount) return;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    mount->backend->enumerate(mount->backend, phys, cb, user);
}

bool mel_vfs_exists(str8 path)
{
    assert(s_vfs.initialized);

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, false);
    if (!mount) return false;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    return mount->backend->exists(mount->backend, phys);
}

bool mel_vfs_mkdir(str8 path)
{
    assert(s_vfs.initialized);

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, true);
    if (!mount) return false;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    return mount->backend->mkdir(mount->backend, phys);
}

bool mel_vfs_delete(str8 path)
{
    assert(s_vfs.initialized);

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, true);
    if (!mount) return false;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    return mount->backend->delete_fn(mount->backend, phys);
}

Mel_Vfs_Handle mel_vfs_open(str8 path, u32 flags)
{
    assert(s_vfs.initialized);

    bool need_write = (flags & (MEL_VFS_OPEN_WRITE | MEL_VFS_OPEN_CREATE | MEL_VFS_OPEN_TRUNCATE)) != 0;

    u8 norm_buf[MEL_VFS_PATH_MAX];
    str8 norm = mel__vfs_normalize_stack(path, norm_buf, MEL_VFS_PATH_MAX);

    Mel__Vfs_Mount* mount = mel__vfs_find_mount(norm, need_write);
    if (!mount) return MEL_VFS_HANDLE_NULL;

    str8 stripped = mel__vfs_strip_prefix(norm, mount->prefix);
    u8 phys_buf[MEL_VFS_PATH_MAX];
    str8 phys = mel__vfs_resolve_physical(stripped, mount->root, phys_buf, MEL_VFS_PATH_MAX);

    i32 backend_handle = mount->backend->open(mount->backend, phys, flags);
    if (backend_handle < 0) return MEL_VFS_HANDLE_NULL;

    for (u32 i = 0; i < MEL_VFS_MAX_OPEN_FILES; i++)
    {
        if (!s_vfs.files[i].active)
        {
            u32 gen = s_vfs.generation_counter++;
            s_vfs.files[i] = (Mel__Vfs_File_Slot){
                .backend = mount->backend,
                .backend_handle = backend_handle,
                .generation = gen,
                .active = true,
            };
            return (Mel_Vfs_Handle){ .index = i, .generation = gen };
        }
    }

    mount->backend->close(mount->backend, backend_handle);
    assert(false && "VFS open file table exhausted");
    return MEL_VFS_HANDLE_NULL;
}

static Mel__Vfs_File_Slot* mel__vfs_get_slot(Mel_Vfs_Handle fh)
{
    assert(fh.index < MEL_VFS_MAX_OPEN_FILES);
    Mel__Vfs_File_Slot* slot = &s_vfs.files[fh.index];
    assert(slot->active && slot->generation == fh.generation);
    return slot;
}

void mel_vfs_close(Mel_Vfs_Handle fh)
{
    Mel__Vfs_File_Slot* slot = mel__vfs_get_slot(fh);
    slot->backend->close(slot->backend, slot->backend_handle);
    slot->active = false;
}

i64 mel_vfs_read_handle(Mel_Vfs_Handle fh, void* buf, i64 size, i64 offset)
{
    Mel__Vfs_File_Slot* slot = mel__vfs_get_slot(fh);

    if (slot->backend->caps & MEL_VFS_CAP_ASYNC)
    {
        i64 aio_result = 0;
        i32 aio_err = 0;
        Mel_Counter counter = MEL_COUNTER_INIT;
        mel_counter_increment(&counter);

        Mel_Aio_Op op = {
            .fd = slot->backend_handle,
            .buf = buf,
            .size = size,
            .offset = offset,
            .counter = &counter,
            .result = &aio_result,
            .error = &aio_err,
        };

        mel_aio_submit(&op);
        mel__vfs_drain_wait(&counter);
        return aio_err == 0 ? aio_result : -1;
    }

    return slot->backend->read(slot->backend, slot->backend_handle, buf, size, offset);
}

i64 mel_vfs_write_handle(Mel_Vfs_Handle fh, const void* buf, i64 size, i64 offset)
{
    Mel__Vfs_File_Slot* slot = mel__vfs_get_slot(fh);
    return slot->backend->write(slot->backend, slot->backend_handle, buf, size, offset);
}

i64 mel_vfs_file_length(Mel_Vfs_Handle fh)
{
    Mel__Vfs_File_Slot* slot = mel__vfs_get_slot(fh);
    return slot->backend->file_length(slot->backend, slot->backend_handle);
}

Mel_Vfs_Map mel_vfs_map(Mel_Vfs_Handle fh, i64 offset, i64 size, u32 prot)
{
    Mel__Vfs_File_Slot* slot = mel__vfs_get_slot(fh);
    assert(slot->backend->map);
    void* ptr = slot->backend->map(slot->backend, slot->backend_handle, offset, size, prot);
    if (!ptr) return MEL_VFS_MAP_NULL;
    return (Mel_Vfs_Map){ .ptr = ptr, .size = size, .backend = slot->backend };
}

void mel_vfs_unmap(Mel_Vfs_Map map)
{
    assert(map.ptr != NULL);
    assert(map.backend != NULL);
    assert(map.backend->unmap);
    map.backend->unmap(map.backend, map.ptr, map.size);
}
