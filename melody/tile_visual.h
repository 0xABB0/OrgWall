#ifndef MEL_ASSETS_TILE_VISUAL_H
#define MEL_ASSETS_TILE_VISUAL_H

#include "types.h"
#include "allocator.fwd.h"
#include "vk_texture.h"

#define MEL_TILE_FLAG_SOLID   (1 << 0)
#define MEL_TILE_FLAG_WATER   (1 << 1)
#define MEL_TILE_FLAG_HAZARD  (1 << 2)

typedef struct
{
    u32 id;
    u32 source_idx;
    u32 source_x, source_y;
    u32 width, height;
    u32 flags;
    const char** tags;
    u32 tag_count;
} Mel_TileEntry;

typedef struct
{
    const char* path;
    Mel_VkTexture* texture;
    u32 tile_width, tile_height;
    u32 columns, rows;
    u32 padding, margin;
    bool is_sheet;
} Mel_TileSource;

typedef struct Mel_TileVisual
{
    const Mel_Alloc* alloc;
    const char* name;
    Mel_TileSource* sources;
    u32 source_count;
    Mel_TileEntry* tiles;
    u32 tile_count;
} Mel_TileVisual;

bool mel_tile_visual_load(Mel_TileVisual* visual, const Mel_Alloc* alloc, const char* path);
bool mel_tile_visual_save(Mel_TileVisual* visual, const char* path);
void mel_tile_visual_free(Mel_TileVisual* visual);

Mel_TileSource* mel_tile_visual_add_source(Mel_TileVisual* visual, const char* texture_path, bool is_sheet);
bool mel_tile_visual_remove_source(Mel_TileVisual* visual, u32 source_idx);

void mel_tile_visual_regenerate_tiles(Mel_TileVisual* visual);

Mel_TileEntry* mel_tile_visual_get_tile(Mel_TileVisual* visual, u32 tile_id);
void mel_tile_visual_get_tile_uv(Mel_TileVisual* visual, u32 tile_id, f32* u0, f32* v0, f32* u1, f32* v1);

bool mel_tile_visual_tile_has_tag(Mel_TileVisual* visual, u32 tile_id, const char* tag);
void mel_tile_visual_tile_add_tag(Mel_TileVisual* visual, u32 tile_id, const char* tag);
void mel_tile_visual_tile_remove_tag(Mel_TileVisual* visual, u32 tile_id, const char* tag);

#endif
