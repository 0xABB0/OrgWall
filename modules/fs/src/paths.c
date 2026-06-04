#include "fs_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>

Mel_Fs_Path_Result mel_fs_folder(Mel_Fs_Folder folder, const Mel_Alloc* alloc) { return mel_fs__backend_folder(folder, alloc ? alloc : mel_alloc_heap()); }

Mel_Fs_Path_Result mel_fs_cwd(const Mel_Alloc* alloc) { return mel_fs__backend_cwd(alloc ? alloc : mel_alloc_heap()); }

Mel_Fs_Void_Result mel_fs_chdir(str8 path) { return mel_fs__backend_chdir(path); }
