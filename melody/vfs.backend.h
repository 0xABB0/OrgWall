#pragma once

#include "vfs.fwd.h"
#include "string.str8.fwd.h"

typedef usize Mel_Vfs_Native_Handle;

typedef struct Mel_Vfs Mel_Vfs;

struct Mel_Vfs_Backend {
    i32  (*open)(Mel_Vfs_Backend*, str8 path, u32 open_flags, Mel_Vfs_Native_Handle* out);
    void (*close)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h);
    i64  (*read)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h, u64 offset, void* dst, usize size);
    i64  (*write)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h, u64 offset, const void* src, usize size);
    i64  (*readv)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h, u64 offset, Mel_IoVec* iov, usize iov_cnt);
    i64  (*writev)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h, u64 offset, const Mel_IoVec* iov, usize iov_cnt);
    i32  (*stat)(Mel_Vfs_Backend*, str8 path, Mel_Vfs_Stat* out);
    i32  (*dir_open)(Mel_Vfs_Backend*, str8 path, Mel_Vfs_Native_Handle* out);
    i32  (*dir_next)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h, u8* name_buf, usize name_cap, usize* out_name_len, Mel_Vfs_Stat* out_stat);
    void (*dir_close)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h);
    i32  (*map)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h, u64 offset, usize size, u32 flags, void** out_ptr);
    void (*unmap)(Mel_Vfs_Backend*, void* ptr, usize size);
    i32  (*watch_open)(Mel_Vfs_Backend*, str8 path, bool recursive, u32 flags, Mel_Vfs_Native_Handle* out);
    i32  (*watch_next)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h, i32 timeout_ms, u8* path_buf, usize path_cap, usize* out_path_len, i32* out_action);
    void (*watch_close)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h);
    i32  (*sync)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle h);
    i32  (*rename)(Mel_Vfs_Backend*, str8 old_path, str8 new_path);
    i32  (*remove)(Mel_Vfs_Backend*, str8 path);
    i32  (*mkdir)(Mel_Vfs_Backend*, str8 path);
    bool (*try_submit_native)(Mel_Vfs_Backend*, struct Mel_Vfs_Sqe* sqe, Mel_Vfs_Native_Handle h, Mel_Vfs* vfs, void* op);
    void (*destroy)(Mel_Vfs_Backend*);
    void* impl_data;
};
