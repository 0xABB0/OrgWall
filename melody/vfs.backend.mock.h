#pragma once

#include "vfs.fwd.h"
#include "allocator.fwd.h"

typedef struct Mel_Vfs_Mock Mel_Vfs_Mock;

Mel_Vfs_Mock*    mel_vfs_mock_create(const Mel_Alloc* alloc);
void             mel_vfs_mock_destroy(Mel_Vfs_Mock* mock);
Mel_Vfs_Backend* mel_vfs_mock_backend(Mel_Vfs_Mock* mock);
void             mel_vfs_mock_add_file(Mel_Vfs_Mock* mock, const char* path, const void* data, i64 size);
