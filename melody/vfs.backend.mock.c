#include "vfs.backend.mock.h"
#include "vfs.h"
#include "string.str8.h"
#include "allocator.h"

#include <string.h>
#include <assert.h>

#define MOCK_MAX_FILES 64

typedef struct {
    char path[256];
    u8* data;
    i64 size;
    bool active;
} Mel__Mock_File;

typedef struct {
    i32 file_index;
    bool active;
} Mel__Mock_Handle;

#define MOCK_MAX_HANDLES 32

struct Mel_Vfs_Mock {
    Mel_Vfs_Backend backend;
    Mel__Mock_File files[MOCK_MAX_FILES];
    u32 file_count;
    Mel__Mock_Handle handles[MOCK_MAX_HANDLES];
    const Mel_Alloc* alloc;
};

static i32 mel__mock_find_file(Mel_Vfs_Mock* mock, str8 path)
{
    for (u32 i = 0; i < mock->file_count; i++)
    {
        if (!mock->files[i].active) continue;
        if (strlen(mock->files[i].path) != (size_t)path.len) continue;
        if (memcmp(mock->files[i].path, path.data, (size_t)path.len) == 0)
            return (i32)i;
    }
    return -1;
}

static i32 mel__mock_open(Mel_Vfs_Backend* self, str8 path, u32 flags)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;

    i32 file_idx = mel__mock_find_file(mock, path);

    if (file_idx < 0 && (flags & MEL_VFS_OPEN_CREATE))
    {
        for (u32 i = 0; i < MOCK_MAX_FILES; i++)
        {
            if (!mock->files[i].active)
            {
                size_t len = (size_t)path.len < sizeof(mock->files[i].path) - 1
                    ? (size_t)path.len : sizeof(mock->files[i].path) - 1;
                memcpy(mock->files[i].path, path.data, len);
                mock->files[i].path[len] = 0;
                mock->files[i].data = NULL;
                mock->files[i].size = 0;
                mock->files[i].active = true;
                if (i >= mock->file_count) mock->file_count = i + 1;
                file_idx = (i32)i;
                break;
            }
        }
    }

    if (file_idx < 0) return -1;

    if (flags & MEL_VFS_OPEN_TRUNCATE)
    {
        if (mock->files[file_idx].data)
        {
            mel_dealloc(mock->alloc, mock->files[file_idx].data);
            mock->files[file_idx].data = NULL;
        }
        mock->files[file_idx].size = 0;
    }

    for (u32 i = 0; i < MOCK_MAX_HANDLES; i++)
    {
        if (!mock->handles[i].active)
        {
            mock->handles[i].file_index = file_idx;
            mock->handles[i].active = true;
            return (i32)i;
        }
    }

    return -1;
}

static void mel__mock_close(Mel_Vfs_Backend* self, i32 handle)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;
    assert(handle >= 0 && handle < MOCK_MAX_HANDLES);
    mock->handles[handle].active = false;
}

static i64 mel__mock_read(Mel_Vfs_Backend* self, i32 handle, void* buf, i64 size, i64 offset)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;
    assert(handle >= 0 && handle < MOCK_MAX_HANDLES);
    assert(mock->handles[handle].active);

    Mel__Mock_File* f = &mock->files[mock->handles[handle].file_index];
    if (offset >= f->size) return 0;

    i64 available = f->size - offset;
    i64 to_read = size < available ? size : available;
    memcpy(buf, f->data + offset, (size_t)to_read);
    return to_read;
}

static i64 mel__mock_write(Mel_Vfs_Backend* self, i32 handle, const void* buf, i64 size, i64 offset)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;
    assert(handle >= 0 && handle < MOCK_MAX_HANDLES);
    assert(mock->handles[handle].active);

    Mel__Mock_File* f = &mock->files[mock->handles[handle].file_index];

    i64 needed = offset + size;
    if (needed > f->size)
    {
        u8* new_data = mel_alloc(mock->alloc, (usize)needed);
        if (f->data)
        {
            memcpy(new_data, f->data, (size_t)f->size);
            mel_dealloc(mock->alloc, f->data);
        }
        memset(new_data + f->size, 0, (size_t)(needed - f->size));
        f->data = new_data;
        f->size = needed;
    }

    memcpy(f->data + offset, buf, (size_t)size);
    return size;
}

static i64 mel__mock_file_length(Mel_Vfs_Backend* self, i32 handle)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;
    assert(handle >= 0 && handle < MOCK_MAX_HANDLES);
    assert(mock->handles[handle].active);
    return mock->files[mock->handles[handle].file_index].size;
}

static bool mel__mock_stat(Mel_Vfs_Backend* self, str8 path, Mel_Vfs_Stat* out)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;
    i32 idx = mel__mock_find_file(mock, path);
    if (idx < 0) return false;

    out->size = (u64)mock->files[idx].size;
    out->flags = MEL_VFS_STAT_IS_FILE;
    return true;
}

static bool mel__mock_exists(Mel_Vfs_Backend* self, str8 path)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;
    return mel__mock_find_file(mock, path) >= 0;
}

static bool mel__mock_enumerate(Mel_Vfs_Backend* self, str8 path, Mel_Vfs_Enumerate_Cb cb, void* user)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;
    (void)path;

    for (u32 i = 0; i < mock->file_count; i++)
    {
        if (!mock->files[i].active) continue;
        Mel_Vfs_Stat st = { .size = (u64)mock->files[i].size, .flags = MEL_VFS_STAT_IS_FILE };
        str8 name = str8_from_cstr(mock->files[i].path);
        if (!cb(name, &st, user)) break;
    }
    return true;
}

static bool mel__mock_delete(Mel_Vfs_Backend* self, str8 path)
{
    Mel_Vfs_Mock* mock = (Mel_Vfs_Mock*)self;
    i32 idx = mel__mock_find_file(mock, path);
    if (idx < 0) return false;

    if (mock->files[idx].data)
        mel_dealloc(mock->alloc, mock->files[idx].data);
    mock->files[idx].active = false;
    return true;
}

static bool mel__mock_mkdir(Mel_Vfs_Backend* self, str8 path)
{
    (void)self; (void)path;
    return true;
}

Mel_Vfs_Mock* mel_vfs_mock_create(const Mel_Alloc* alloc)
{
    Mel_Vfs_Mock* mock = mel_alloc(alloc, sizeof(Mel_Vfs_Mock));
    memset(mock, 0, sizeof(Mel_Vfs_Mock));
    mock->alloc = alloc;

    mock->backend = (Mel_Vfs_Backend){
        .caps = MEL_VFS_CAP_READ | MEL_VFS_CAP_WRITE | MEL_VFS_CAP_SEEK,
        .open = mel__mock_open,
        .close = mel__mock_close,
        .read = mel__mock_read,
        .write = mel__mock_write,
        .file_length = mel__mock_file_length,
        .stat = mel__mock_stat,
        .enumerate = mel__mock_enumerate,
        .mkdir = mel__mock_mkdir,
        .delete_fn = mel__mock_delete,
        .exists = mel__mock_exists,
        .map = NULL,
        .unmap = NULL,
    };

    return mock;
}

void mel_vfs_mock_destroy(Mel_Vfs_Mock* mock)
{
    for (u32 i = 0; i < mock->file_count; i++)
    {
        if (mock->files[i].active && mock->files[i].data)
            mel_dealloc(mock->alloc, mock->files[i].data);
    }

    const Mel_Alloc* alloc = mock->alloc;
    mel_dealloc(alloc, mock);
}

Mel_Vfs_Backend* mel_vfs_mock_backend(Mel_Vfs_Mock* mock)
{
    return &mock->backend;
}

void mel_vfs_mock_add_file(Mel_Vfs_Mock* mock, const char* path, const void* data, i64 size)
{
    assert(mock->file_count < MOCK_MAX_FILES);

    u32 slot = mock->file_count++;
    strncpy(mock->files[slot].path, path, sizeof(mock->files[slot].path) - 1);
    mock->files[slot].path[sizeof(mock->files[slot].path) - 1] = 0;

    if (data && size > 0)
    {
        mock->files[slot].data = mel_alloc(mock->alloc, (usize)size);
        memcpy(mock->files[slot].data, data, (size_t)size);
    }

    mock->files[slot].size = size;
    mock->files[slot].active = true;
}
