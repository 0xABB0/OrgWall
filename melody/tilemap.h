#ifndef MEL_ASSETS_TILEMAP_H
#define MEL_ASSETS_TILEMAP_H

#include "types.h"
#include "allocator.h"

typedef struct Mel_TileVisual Mel_TileVisual;

typedef struct
{
    const char* name;
    i32* tiles;
    u32 width;
    u32 height;
    bool visible;
    f32 parallax_x, parallax_y;
    i32 offset_x, offset_y;
} Mel_TilemapLayer;

typedef struct
{
    const Mel_Alloc* alloc;
    const char* name;
    const char* tile_visual_path;
    u32 width;
    u32 height;
    u32 grid_width;
    u32 grid_height;
    Mel_TilemapLayer* layers;
    u32 layer_count;
    Mel_TileVisual* tile_visual;
} Mel_Tilemap;

bool mel_tilemap_load(Mel_Tilemap* tilemap, const Mel_Alloc* alloc, const char* path);
bool mel_tilemap_save(Mel_Tilemap* tilemap, const char* path);
void mel_tilemap_free(Mel_Tilemap* tilemap);

i32 mel_tilemap_get_tile(Mel_Tilemap* tilemap, u32 layer, u32 x, u32 y);
void mel_tilemap_set_tile(Mel_Tilemap* tilemap, u32 layer, u32 x, u32 y, i32 tile);

Mel_TilemapLayer* mel_tilemap_add_layer(Mel_Tilemap* tilemap, const char* name);
bool mel_tilemap_remove_layer(Mel_Tilemap* tilemap, u32 layer_idx);
bool mel_tilemap_move_layer(Mel_Tilemap* tilemap, u32 from_idx, u32 to_idx);

bool mel_tilemap_resize(Mel_Tilemap* tilemap, u32 new_width, u32 new_height);

#endif
