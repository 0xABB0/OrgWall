#pragma once

#include "core.types.h"
#include "collection.slotmap.fwd.h"
#include "string.str8.fwd.h"

typedef struct Mel_Vfs Mel_Vfs;
typedef struct Mel_Vfs_Backend Mel_Vfs_Backend;

typedef struct { Mel_SlotMap_Handle handle; } Mel_Vfs_File;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Vfs_Dir;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Vfs_Map;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Vfs_Watch;

#define MEL_VFS_FILE_INVALID  ((Mel_Vfs_File){.handle = MEL_SLOTMAP_HANDLE_NULL})
#define MEL_VFS_DIR_INVALID   ((Mel_Vfs_Dir){.handle = MEL_SLOTMAP_HANDLE_NULL})
#define MEL_VFS_MAP_INVALID   ((Mel_Vfs_Map){.handle = MEL_SLOTMAP_HANDLE_NULL})
#define MEL_VFS_WATCH_INVALID ((Mel_Vfs_Watch){.handle = MEL_SLOTMAP_HANDLE_NULL})

typedef struct {
    u64 size;
    u64 mtime_ns;
    u64 change_ns;
    u64 birth_ns;
    u32 flags;
} Mel_Vfs_Stat;

#define MEL_VFS_STAT_IS_FILE       (1u << 0)
#define MEL_VFS_STAT_IS_DIR        (1u << 1)
#define MEL_VFS_STAT_IS_SYMLINK    (1u << 2)
#define MEL_VFS_STAT_IS_READONLY   (1u << 3)
#define MEL_VFS_STAT_HAS_BIRTH_TIME (1u << 4)

typedef struct {
    u16 category;
    u16 backend_id;
    i32 code;
    i32 native_code;
} Mel_Vfs_Error;

typedef struct {
    void* buffer;
    usize len;
} Mel_IoVec;

typedef struct {
    str8 name;
    Mel_Vfs_Stat stat;
} Mel_Vfs_Dir_Entry;

typedef struct Mel_Vfs_Sqe Mel_Vfs_Sqe;
typedef struct Mel_Vfs_Cqe Mel_Vfs_Cqe;
typedef struct Mel_Vfs_Mount Mel_Vfs_Mount;
