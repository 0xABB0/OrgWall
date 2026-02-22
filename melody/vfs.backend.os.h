#pragma once

#include "vfs.backend.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"

Mel_Vfs_Backend* mel_vfs_backend_os_create(const Mel_Alloc* alloc, str8 root_path);
void             mel_vfs_backend_os_destroy(Mel_Vfs_Backend* backend);
