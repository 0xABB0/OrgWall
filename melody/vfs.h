#pragma once

#include "vfs.async.h"
#include "vfs.backend.h"
#include "async.io.h"
#include "allocator.h"
#include "collection.array.h"
#include "collection.slotmap.h"
#include "string.str8.fwd.h"

typedef struct {
    Mel_Vfs_Backend* backend;
    Mel_Vfs_Native_Handle native_handle;
    u32 mount_generation;
    u32 mount_index;
} Mel_Vfs__File_Data;

typedef struct {
    Mel_Vfs_Backend* backend;
    Mel_Vfs_Native_Handle native_handle;
} Mel_Vfs__Dir_Data;

typedef struct {
    void* ptr;
    usize size;
    Mel_Vfs_Backend* backend;
    Mel_Vfs_Native_Handle file_native_handle;
} Mel_Vfs__Map_Data;

typedef struct {
    Mel_Vfs_Backend* backend;
    Mel_Vfs_Native_Handle native_handle;
} Mel_Vfs__Watch_Data;

typedef struct {
    Mel_Vfs_Sqe sqe;
    Mel_Vfs_Cqe cqe;
    u8* owned_path_data;
    Mel_IoVec* owned_iov;
    u8 inline_path[256];
    Mel_IoVec inline_iov[8];
} Mel_Vfs__Op;

struct Mel_Vfs_Mount {
    str8             prefix;
    Mel_Vfs_Backend* backend;
    u8               priority;
    bool             writable;
    u32              refcount;
    bool             retired;
    u32              insertion_order;
};

#include "allocator.pool.h"

struct Mel_Vfs {
    const Mel_Alloc* alloc;

    Mel_Io* io;
    u16     handler_id;

    SDL_Mutex* state_lock;

    Mel_SlotMap file_slots;
    Mel_SlotMap dir_slots;
    Mel_SlotMap map_slots;
    Mel_SlotMap watch_slots;

    Mel_Array(Mel_Vfs_Mount) mounts;
    u32 mount_generation;
    u32 mount_insertion_counter;

    SDL_Mutex* op_lock;
    Mel_Pool   op_pool;
    void*      op_pool_buf;
};

#define MEL_VFS_OP__READ_FILE  100

typedef struct {
    const Mel_Alloc* alloc;
    u8**             out_data;
    usize*           out_size;
} Mel_Vfs__Read_File_Ctx;

typedef bool (*Mel_Vfs_Enum_Cb)(str8 virtual_path, const Mel_Vfs_Stat* stat, void* user);

typedef struct {
    bool recursive;
    bool include_dirs;
} Mel_Vfs_Enum_Opt;

void mel_vfs_mount(Mel_Vfs* vfs, str8 prefix, Mel_Vfs_Backend* backend, u8 priority, bool writable);
void mel_vfs_unmount(Mel_Vfs* vfs, str8 prefix);

u8*  mel_vfs_read_file_alloc(Mel_Vfs* vfs, str8 path, usize* out_size, const Mel_Alloc* alloc);
str8 mel_vfs_read_text_alloc(Mel_Vfs* vfs, str8 path, const Mel_Alloc* alloc);
bool mel_vfs_write_file(Mel_Vfs* vfs, str8 path, const u8* data, usize size);
bool mel_vfs_write_text(Mel_Vfs* vfs, str8 path, str8 text);
bool mel_vfs_exists(Mel_Vfs* vfs, str8 path);
bool mel_vfs_stat_sync(Mel_Vfs* vfs, str8 path, Mel_Vfs_Stat* out);
bool mel_vfs_sync_file(Mel_Vfs* vfs, Mel_Vfs_File file);
bool mel_vfs_rename(Mel_Vfs* vfs, str8 src, str8 dst);
bool mel_vfs_delete(Mel_Vfs* vfs, str8 path);
bool mel_vfs_mkdir(Mel_Vfs* vfs, str8 path);

bool mel_vfs_enumerate_opt(Mel_Vfs* vfs, str8 path, Mel_Vfs_Enum_Cb cb, void* user, Mel_Vfs_Enum_Opt opt);
#define mel_vfs_enumerate(vfs, path, cb, user, ...) \
    mel_vfs_enumerate_opt((vfs), (path), (cb), (user), (Mel_Vfs_Enum_Opt){__VA_ARGS__})

typedef struct {
    u8**             out_data;
    usize*           out_size;
    const Mel_Alloc* alloc;
} Mel_Vfs_Read_File_Async_Opt;

u64 mel_vfs_read_file_async_opt(Mel_Vfs* vfs, str8 path, Mel_Vfs_Read_File_Async_Opt opt);
#define mel_vfs_read_file_async(vfs, path, ...) \
    mel_vfs_read_file_async_opt((vfs), (path), (Mel_Vfs_Read_File_Async_Opt){__VA_ARGS__})
