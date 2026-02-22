#pragma once

#include "vfs.backend.h"
#include "allocator.fwd.h"

Mel_Vfs_Backend* mel_vfs_backend_mock_create(const Mel_Alloc* alloc);
void             mel_vfs_backend_mock_destroy(Mel_Vfs_Backend* backend);

void mel_vfs_backend_mock_inject_file(Mel_Vfs_Backend* backend, str8 path, const u8* data, usize size);
void mel_vfs_backend_mock_inject_dir(Mel_Vfs_Backend* backend, str8 path);
