#pragma once

#include "core.types.h"
#include "vfs.cfg.h"
#include "vfs.fwd.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"

#define MEL_VFS_STAT_IS_FILE     (1u << 0)
#define MEL_VFS_STAT_IS_DIR      (1u << 1)

#define MEL_VFS_OPEN_READ        (1u << 0)
#define MEL_VFS_OPEN_WRITE       (1u << 1)
#define MEL_VFS_OPEN_CREATE      (1u << 2)
#define MEL_VFS_OPEN_TRUNCATE    (1u << 3)

#define MEL_VFS_MAP_READ         (1u << 0)
#define MEL_VFS_MAP_WRITE        (1u << 1)

#define MEL_VFS_CAP_READ         (1u << 0)
#define MEL_VFS_CAP_WRITE        (1u << 1)
#define MEL_VFS_CAP_MMAP         (1u << 2)
#define MEL_VFS_CAP_ASYNC        (1u << 3)
#define MEL_VFS_CAP_SEEK         (1u << 4)

struct Mel_Vfs_Stat {
    u64 size;
    u32 flags;
};

typedef bool (*Mel_Vfs_Enumerate_Cb)(str8 path, const Mel_Vfs_Stat* stat, void* user);

struct Mel_Vfs_Backend {
    u32   caps;
    i32   (*open)(Mel_Vfs_Backend* self, str8 path, u32 flags);
    void  (*close)(Mel_Vfs_Backend* self, i32 handle);
    i64   (*read)(Mel_Vfs_Backend* self, i32 handle, void* buf, i64 size, i64 offset);
    i64   (*write)(Mel_Vfs_Backend* self, i32 handle, const void* buf, i64 size, i64 offset);
    i64   (*file_length)(Mel_Vfs_Backend* self, i32 handle);
    bool  (*stat)(Mel_Vfs_Backend* self, str8 path, Mel_Vfs_Stat* out);
    bool  (*enumerate)(Mel_Vfs_Backend* self, str8 path, Mel_Vfs_Enumerate_Cb cb, void* user);
    bool  (*mkdir)(Mel_Vfs_Backend* self, str8 path);
    bool  (*delete_fn)(Mel_Vfs_Backend* self, str8 path);
    bool  (*exists)(Mel_Vfs_Backend* self, str8 path);
    void* (*map)(Mel_Vfs_Backend* self, i32 handle, i64 offset, i64 size, u32 prot);
    void  (*unmap)(Mel_Vfs_Backend* self, void* ptr, i64 size);
};

typedef struct {
    str8 root;
    u8   priority;
    bool writable;
} Mel_Vfs_Mount_Opt;

void mel_vfs_init(const Mel_Alloc* alloc);
void mel_vfs_shutdown(void);

void mel_vfs_mount_opt(str8 prefix, Mel_Vfs_Backend* backend, Mel_Vfs_Mount_Opt opt);
#define mel_vfs_mount(prefix, backend, ...) \
    mel_vfs_mount_opt((prefix), (backend), (Mel_Vfs_Mount_Opt){__VA_ARGS__})
void mel_vfs_unmount(str8 prefix);

u8*  mel_vfs_read_file(str8 path, i64* out_size, const Mel_Alloc* alloc);
i64  mel_vfs_read(str8 path, void* buf, i64 size, i64 offset);
bool mel_vfs_write_file(str8 path, const void* data, i64 size);
bool mel_vfs_stat(str8 path, Mel_Vfs_Stat* out);
void mel_vfs_enumerate(str8 path, Mel_Vfs_Enumerate_Cb cb, void* user);
bool mel_vfs_exists(str8 path);
bool mel_vfs_mkdir(str8 path);
bool mel_vfs_delete(str8 path);

Mel_Vfs_Handle mel_vfs_open(str8 path, u32 flags);
void           mel_vfs_close(Mel_Vfs_Handle fh);
i64            mel_vfs_read_handle(Mel_Vfs_Handle fh, void* buf, i64 size, i64 offset);
i64            mel_vfs_write_handle(Mel_Vfs_Handle fh, const void* buf, i64 size, i64 offset);
i64            mel_vfs_file_length(Mel_Vfs_Handle fh);

Mel_Vfs_Map    mel_vfs_map(Mel_Vfs_Handle fh, i64 offset, i64 size, u32 prot);
void           mel_vfs_unmap(Mel_Vfs_Map map);
