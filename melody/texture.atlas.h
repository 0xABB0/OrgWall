#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"
#include "texture.atlas.fwd.h"
#include "texture.pool.fwd.h"
#include "collection.slotmap.h"
#include "collection.hashmap.h"

struct Mel_Atlas_Region {
    u32 x, y, width, height;
    i32 offset_x, offset_y;
};

struct Mel_Atlas_Entry {
    Mel_Texture_Handle texture;
    u32 texture_width, texture_height;
    Mel_Atlas_Region* regions;
    u32 region_count;
    u64* region_name_hashes;
    const Mel_Alloc* alloc;
};

struct Mel_Atlas_Pool {
    Mel_SlotMap slotmap;
    Mel_HashMap path_to_handle;
    Mel_Texture_Pool* texture_pool;
    const Mel_Alloc* alloc;
};

void             mel_atlas_pool_init(Mel_Atlas_Pool* pool, const Mel_Alloc* alloc, Mel_Texture_Pool* tex_pool);
void             mel_atlas_pool_shutdown(Mel_Atlas_Pool* pool);
Mel_Atlas_Handle mel_atlas_pool_load(Mel_Atlas_Pool* pool, str8 path);
Mel_Atlas_Entry* mel_atlas_pool_get(Mel_Atlas_Pool* pool, Mel_Atlas_Handle handle);
bool             mel_atlas_pool_unload(Mel_Atlas_Pool* pool, Mel_Atlas_Handle handle);

i32              mel_atlas_find_region(Mel_Atlas_Entry* entry, u64 name_hash);
void             mel_atlas_get_region_uv(Mel_Atlas_Entry* entry, u32 region_idx,
                                         f32* u0, f32* v0, f32* u1, f32* v1);
