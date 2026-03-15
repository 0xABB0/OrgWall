#pragma once

#include "core.types.h"

typedef struct Mel_Vfs_Backend Mel_Vfs_Backend;

typedef struct { u32 index; u32 generation; } Mel_Vfs_Handle;
#define MEL_VFS_HANDLE_NULL ((Mel_Vfs_Handle){0})

typedef struct { void* ptr; i64 size; Mel_Vfs_Backend* backend; } Mel_Vfs_Map;
#define MEL_VFS_MAP_NULL ((Mel_Vfs_Map){0})

typedef struct Mel_Vfs_Stat Mel_Vfs_Stat;
