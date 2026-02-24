#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"
#include "tile.set.fwd.h"
#include "texture.pool.fwd.h"
#include "collection.slotmap.h"
#include "collection.hashmap.h"

#define MEL_TILE_FLAG_SOLID   (1 << 0)
#define MEL_TILE_FLAG_WATER   (1 << 1)
#define MEL_TILE_FLAG_HAZARD  (1 << 2)

struct Mel_Tile_Source {
    str8 path;
    Mel_Texture_Handle texture;
    u32 tile_width, tile_height;
    u32 columns, rows;
    u32 padding, margin;
    bool is_sheet;
};

struct Mel_Tile_Def {
    u32 id;
    u32 source_idx;
    u32 source_x, source_y;
    u32 width, height;
    u32 flags;
};

struct Mel_Tileset_Entry {
    str8 name;
    Mel_Tile_Source* sources;
    u32 source_count;
    Mel_Tile_Def* tiles;
    u32 tile_count;
    const Mel_Alloc* alloc;
};

typedef struct Mel_Vfs Mel_Vfs;

struct Mel_Tileset_Pool {
    Mel_SlotMap slotmap;
    Mel_HashMap path_to_handle;
    Mel_Texture_Pool* texture_pool;
    Mel_Vfs* vfs;
    const Mel_Alloc* alloc;
};

void               mel_tileset_pool_init(Mel_Tileset_Pool* pool, const Mel_Alloc* alloc, Mel_Texture_Pool* tex_pool, Mel_Vfs* vfs);
void               mel_tileset_pool_shutdown(Mel_Tileset_Pool* pool);
Mel_Tileset_Handle mel_tileset_pool_load(Mel_Tileset_Pool* pool, str8 path);
Mel_Tileset_Handle mel_tileset_pool_create(Mel_Tileset_Pool* pool, str8 name);
Mel_Tileset_Entry* mel_tileset_pool_get(Mel_Tileset_Pool* pool, Mel_Tileset_Handle handle);
bool               mel_tileset_pool_unload(Mel_Tileset_Pool* pool, Mel_Tileset_Handle handle);
bool               mel_tileset_pool_save(Mel_Tileset_Pool* pool, Mel_Tileset_Handle handle, str8 path);

Mel_Tile_Def*      mel_tileset_entry_get_tile(Mel_Tileset_Entry* entry, u32 tile_id);
Mel_Tile_Source*   mel_tileset_entry_add_source(Mel_Tileset_Entry* entry, str8 texture_path, bool is_sheet);
bool               mel_tileset_entry_remove_source(Mel_Tileset_Entry* entry, u32 source_idx);
void               mel_tileset_entry_regenerate_tiles(Mel_Tileset_Entry* entry, Mel_Texture_Pool* tex_pool);
