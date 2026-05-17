#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "str8.fwd.h"
#include "tile.map.fwd.h"
#include "tile.set.fwd.h"
#include "collection.slotmap.h"
#include "collection.hashmap.h"

typedef struct {
    str8 name;
    i32* data;
    u32 width, height;
    bool visible;
    f32 parallax_x, parallax_y;
    i32 offset_x, offset_y;
} Mel_Tilemap_Layer;

struct Mel_Tilemap_Entry {
    str8 name;
    Mel_Tileset_Handle tileset;
    Mel_Tilemap_Layer* layers;
    u32 layer_count;
    u32 width, height;
    u32 grid_width, grid_height;
    const Mel_Alloc* alloc;
};

struct Mel_Tilemap_Pool {
    Mel_SlotMap slotmap;
    Mel_HashMap path_to_handle;
    Mel_Tileset_Pool* tileset_pool;
    const Mel_Alloc* alloc;
};

void               mel_tilemap_pool_init(Mel_Tilemap_Pool* pool, const Mel_Alloc* alloc, Mel_Tileset_Pool* ts_pool);
void               mel_tilemap_pool_shutdown(Mel_Tilemap_Pool* pool);
Mel_Tilemap_Handle mel_tilemap_pool_load(Mel_Tilemap_Pool* pool, str8 path);
Mel_Tilemap_Handle mel_tilemap_pool_create(Mel_Tilemap_Pool* pool, str8 name, u32 width, u32 height, u32 grid_width, u32 grid_height);
Mel_Tilemap_Entry* mel_tilemap_pool_get(Mel_Tilemap_Pool* pool, Mel_Tilemap_Handle handle);
bool               mel_tilemap_pool_unload(Mel_Tilemap_Pool* pool, Mel_Tilemap_Handle handle);
bool               mel_tilemap_pool_save(Mel_Tilemap_Pool* pool, Mel_Tilemap_Handle handle, str8 path);

i32                mel_tilemap_entry_get_tile(Mel_Tilemap_Entry* entry, u32 layer, u32 x, u32 y);
void               mel_tilemap_entry_set_tile(Mel_Tilemap_Entry* entry, u32 layer, u32 x, u32 y, i32 tile);
Mel_Tilemap_Layer* mel_tilemap_entry_add_layer(Mel_Tilemap_Entry* entry, str8 name);
bool               mel_tilemap_entry_remove_layer(Mel_Tilemap_Entry* entry, u32 layer_idx);
bool               mel_tilemap_entry_move_layer(Mel_Tilemap_Entry* entry, u32 from_idx, u32 to_idx);
bool               mel_tilemap_entry_resize(Mel_Tilemap_Entry* entry, u32 new_width, u32 new_height);
