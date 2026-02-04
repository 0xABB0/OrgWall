#ifndef MEL_ASSET_REGISTRY_H
#define MEL_ASSET_REGISTRY_H

#include "types.h"
#include "vk_context.h"
#include "vk_pipeline.h"
#include "tile_visual.h"
#include "tilemap.h"
#include "spritesheet.h"

typedef struct
{
    char path[256];
    Mel_VkTexture texture;
    bool loaded;
} Mel_TextureEntry;

typedef struct
{
    char path[256];
    Mel_TileVisual tile_visual;
    bool loaded;
} Mel_TileVisualEntry;

typedef struct
{
    char path[256];
    Mel_Tilemap tilemap;
    bool loaded;
} Mel_TilemapEntry;

typedef struct
{
    char path[256];
    Mel_Spritesheet spritesheet;
    bool loaded;
} Mel_SpritesheetEntry;

typedef struct
{
    Mel_TextureEntry* textures;
    u32 texture_count;
    u32 texture_capacity;

    Mel_TileVisualEntry* tile_visuals;
    u32 tile_visual_count;
    u32 tile_visual_capacity;

    Mel_TilemapEntry* tilemaps;
    u32 tilemap_count;
    u32 tilemap_capacity;

    Mel_SpritesheetEntry* spritesheets;
    u32 spritesheet_count;
    u32 spritesheet_capacity;

    Mel_VkContext* vk;
    Mel_VkPipeline* pipeline;
    const Mel_Alloc* alloc;
} Mel_AssetRegistry;

void mel_asset_registry_init(Mel_AssetRegistry* registry, Mel_VkContext* vk, Mel_VkPipeline* pipeline, const Mel_Alloc* alloc);
void mel_asset_registry_shutdown(Mel_AssetRegistry* registry);

Mel_VkTexture* mel_asset_registry_load_texture(Mel_AssetRegistry* registry, const char* path);
Mel_VkTexture* mel_asset_registry_get_texture(Mel_AssetRegistry* registry, const char* path);
Mel_VkTexture* mel_asset_registry_import_texture(Mel_AssetRegistry* registry, const char* filesystem_path);
bool mel_asset_registry_unload_texture(Mel_AssetRegistry* registry, const char* path);
u32 mel_asset_registry_texture_count(Mel_AssetRegistry* registry);
const char* mel_asset_registry_texture_path(Mel_AssetRegistry* registry, u32 index);

Mel_TileVisual* mel_asset_registry_load_tile_visual(Mel_AssetRegistry* registry, const char* path);
Mel_TileVisual* mel_asset_registry_get_tile_visual(Mel_AssetRegistry* registry, const char* path);
Mel_TileVisual* mel_asset_registry_create_tile_visual(Mel_AssetRegistry* registry, const char* name);
bool mel_asset_registry_save_tile_visual(Mel_AssetRegistry* registry, Mel_TileVisual* visual, const char* path);
bool mel_asset_registry_unload_tile_visual(Mel_AssetRegistry* registry, const char* path);
u32 mel_asset_registry_tile_visual_count(Mel_AssetRegistry* registry);
Mel_TileVisual* mel_asset_registry_tile_visual_at(Mel_AssetRegistry* registry, u32 index);
const char* mel_asset_registry_tile_visual_path(Mel_AssetRegistry* registry, u32 index);

Mel_Tilemap* mel_asset_registry_load_tilemap(Mel_AssetRegistry* registry, const char* path);
Mel_Tilemap* mel_asset_registry_get_tilemap(Mel_AssetRegistry* registry, const char* path);
Mel_Tilemap* mel_asset_registry_create_tilemap(Mel_AssetRegistry* registry, const char* name, u32 width, u32 height, u32 grid_width, u32 grid_height);
bool mel_asset_registry_save_tilemap(Mel_AssetRegistry* registry, Mel_Tilemap* tilemap, const char* path);
bool mel_asset_registry_unload_tilemap(Mel_AssetRegistry* registry, const char* path);
u32 mel_asset_registry_tilemap_count(Mel_AssetRegistry* registry);
Mel_Tilemap* mel_asset_registry_tilemap_at(Mel_AssetRegistry* registry, u32 index);
const char* mel_asset_registry_tilemap_path(Mel_AssetRegistry* registry, u32 index);

Mel_Spritesheet* mel_asset_registry_load_spritesheet(Mel_AssetRegistry* registry, const char* path);
Mel_Spritesheet* mel_asset_registry_get_spritesheet(Mel_AssetRegistry* registry, const char* path);
Mel_Spritesheet* mel_asset_registry_create_spritesheet(Mel_AssetRegistry* registry, const char* name, const char* texture_path);
bool mel_asset_registry_save_spritesheet(Mel_AssetRegistry* registry, Mel_Spritesheet* sheet, const char* path);
bool mel_asset_registry_unload_spritesheet(Mel_AssetRegistry* registry, const char* path);
u32 mel_asset_registry_spritesheet_count(Mel_AssetRegistry* registry);
Mel_Spritesheet* mel_asset_registry_spritesheet_at(Mel_AssetRegistry* registry, u32 index);

void mel_asset_registry_scan_directory(Mel_AssetRegistry* registry, const char* dir);

#endif
