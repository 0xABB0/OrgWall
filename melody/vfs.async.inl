#pragma once

#ifdef _CLANGD
#include "vfs.async.h"
#endif

static inline bool mel_vfs_file_valid(Mel_Vfs_File h)
{
    return mel_slotmap_handle_valid(h.handle);
}

static inline bool mel_vfs_dir_valid(Mel_Vfs_Dir h)
{
    return mel_slotmap_handle_valid(h.handle);
}

static inline bool mel_vfs_map_valid(Mel_Vfs_Map h)
{
    return mel_slotmap_handle_valid(h.handle);
}

static inline bool mel_vfs_watch_valid(Mel_Vfs_Watch h)
{
    return mel_slotmap_handle_valid(h.handle);
}
