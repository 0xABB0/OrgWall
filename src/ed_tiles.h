#ifndef MEL_ED_TILES_H
#define MEL_ED_TILES_H

#include "types.h"
#include "allocator.fwd.h"
#include "tile_visual.h"
#include "tilemap.h"
#include "asset_registry.h"
#include "vk_texture.h"

#define MEL_ED_TILES_TOOL_BRUSH   0
#define MEL_ED_TILES_TOOL_SELECT  1
#define MEL_ED_TILES_TOOL_ERASE   2
#define MEL_ED_TILES_TOOL_FILL    3

#define MEL_ED_TILES_BRUSH_HISTORY_SIZE 16

typedef struct
{
    i32* tiles;
    u32 width, height;
} Mel_TileBrush;

typedef struct
{
    Mel_TileBrush current;
    Mel_TileBrush history[MEL_ED_TILES_BRUSH_HISTORY_SIZE];
    u32 history_count;
    bool flip_h, flip_v;
    u32 rotation;
} Mel_BrushState;

typedef struct
{
    bool active;
    i32 start_x, start_y;
    i32 end_x, end_y;
} Mel_SelectionState;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_TileVisual* tile_visual;
    Mel_Tilemap* tilemap;
    Mel_AssetRegistry* registry;

    i32 selected_layer;
    i32 tool;

    Mel_BrushState brush;
    Mel_SelectionState selection;

    f32 zoom;
    f32 scroll_x, scroll_y;
    bool show_grid;

    bool painting;
    i32 last_paint_x, last_paint_y;

    char tile_visual_path[256];
    char tilemap_path[256];
    char new_tile_visual_name[256];
    char new_tilemap_name[256];
    i32 new_tilemap_width;
    i32 new_tilemap_height;
    i32 new_tilemap_grid_w;
    i32 new_tilemap_grid_h;
    char new_layer_name[64];

    char import_texture_path[256];
    i32 import_tile_width;
    i32 import_tile_height;
    i32 import_columns;
    i32 import_rows;
    i32 import_padding;
    i32 import_margin;
    Mel_VkTexture* import_preview_texture;

    bool show_import_dialog;
    bool dirty;

    i32 renaming_layer;
    char rename_layer_buffer[64];
} Mel_EdTiles;

void mel_ed_tiles_init(Mel_EdTiles* ed, const Mel_Alloc* alloc);
void mel_ed_tiles_shutdown(Mel_EdTiles* ed);

void mel_ed_tiles_set_registry(Mel_EdTiles* ed, Mel_AssetRegistry* registry);
void mel_ed_tiles_set_tile_visual(Mel_EdTiles* ed, Mel_TileVisual* visual, const char* path);
void mel_ed_tiles_set_tilemap(Mel_EdTiles* ed, Mel_Tilemap* tilemap, const char* path);

void mel_ed_tiles_draw(Mel_EdTiles* ed);

void mel_ed_tiles_brush_set_single(Mel_EdTiles* ed, i32 tile_id);
void mel_ed_tiles_brush_from_selection(Mel_EdTiles* ed);
void mel_ed_tiles_brush_push_history(Mel_EdTiles* ed);
void mel_ed_tiles_brush_flip_h(Mel_EdTiles* ed);
void mel_ed_tiles_brush_flip_v(Mel_EdTiles* ed);
void mel_ed_tiles_brush_rotate_cw(Mel_EdTiles* ed);

void mel_ed_tiles_paint(Mel_EdTiles* ed, i32 x, i32 y);
void mel_ed_tiles_erase(Mel_EdTiles* ed, i32 x, i32 y);
void mel_ed_tiles_fill(Mel_EdTiles* ed, i32 x, i32 y);

#endif
