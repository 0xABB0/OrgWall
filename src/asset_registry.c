#include "asset_registry.h"
#include "texture.h"
#include "assets.h"
#include "memory.h"
#include <cjson/cJSON.h>
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>

void mel_asset_registry_init(Mel_AssetRegistry* registry, Mel_VkContext* vk, Mel_VkPipeline* pipeline, const Mel_Alloc* alloc)
{
    assert(registry != nullptr);
    assert(vk != nullptr);

    *registry = (Mel_AssetRegistry){0};
    registry->vk = vk;
    registry->pipeline = pipeline;
    registry->alloc = alloc;

    registry->texture_capacity = 64;
    registry->textures = mel_calloc(registry->alloc, registry->texture_capacity * sizeof(Mel_TextureEntry));

    registry->tile_visual_capacity = 32;
    registry->tile_visuals = mel_calloc(registry->alloc, registry->tile_visual_capacity * sizeof(Mel_TileVisualEntry));

    registry->tilemap_capacity = 32;
    registry->tilemaps = mel_calloc(registry->alloc, registry->tilemap_capacity * sizeof(Mel_TilemapEntry));

    registry->spritesheet_capacity = 32;
    registry->spritesheets = mel_calloc(registry->alloc, registry->spritesheet_capacity * sizeof(Mel_SpritesheetEntry));
}

void mel_asset_registry_shutdown(Mel_AssetRegistry* registry)
{
    assert(registry != nullptr);

    for (u32 i = 0; i < registry->texture_count; i++)
    {
        if (registry->textures[i].loaded)
        {
            mel_vk_texture_shutdown(&registry->textures[i].texture, registry->vk);
        }
    }
    mel_free(registry->alloc, registry->textures);

    for (u32 i = 0; i < registry->tile_visual_count; i++)
    {
        if (registry->tile_visuals[i].loaded)
        {
            mel_tile_visual_free(&registry->tile_visuals[i].tile_visual);
        }
    }
    mel_free(registry->alloc, registry->tile_visuals);

    for (u32 i = 0; i < registry->tilemap_count; i++)
    {
        if (registry->tilemaps[i].loaded)
        {
            mel_tilemap_free(&registry->tilemaps[i].tilemap);
        }
    }
    mel_free(registry->alloc, registry->tilemaps);

    for (u32 i = 0; i < registry->spritesheet_count; i++)
    {
        if (registry->spritesheets[i].loaded)
        {
            mel_spritesheet_free(&registry->spritesheets[i].spritesheet);
        }
    }
    mel_free(registry->alloc, registry->spritesheets);

    *registry = (Mel_AssetRegistry){0};
}

Mel_VkTexture* mel_asset_registry_load_texture(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    Mel_VkTexture* existing = mel_asset_registry_get_texture(registry, path);
    if (existing) return existing;

    if (registry->texture_count >= registry->texture_capacity)
    {
        registry->texture_capacity *= 2;
        registry->textures = mel_realloc(registry->alloc, registry->textures, registry->texture_capacity * sizeof(Mel_TextureEntry));
    }

    Mel_TextureEntry* entry = &registry->textures[registry->texture_count];
    strncpy(entry->path, path, sizeof(entry->path) - 1);

    if (!mel_texture_load_and_bind(&entry->texture, registry->vk, registry->pipeline, path))
    {
        return nullptr;
    }

    entry->loaded = true;
    registry->texture_count++;

    return &entry->texture;
}

Mel_VkTexture* mel_asset_registry_import_texture(Mel_AssetRegistry* registry, const char* filesystem_path)
{
    assert(registry != nullptr);
    assert(filesystem_path != nullptr);

    const char* filename = strrchr(filesystem_path, '/');
    if (filename)
    {
        filename++;
    }
    else
    {
        filename = filesystem_path;
    }

    if (!mel_assets_import_file(filesystem_path, filename))
    {
        SDL_Log("Failed to import texture: %s", filesystem_path);
        return nullptr;
    }

    return mel_asset_registry_load_texture(registry, filename);
}

Mel_VkTexture* mel_asset_registry_get_texture(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    for (u32 i = 0; i < registry->texture_count; i++)
    {
        if (registry->textures[i].loaded && strcmp(registry->textures[i].path, path) == 0)
        {
            return &registry->textures[i].texture;
        }
    }

    return nullptr;
}

bool mel_asset_registry_unload_texture(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    for (u32 i = 0; i < registry->texture_count; i++)
    {
        if (registry->textures[i].loaded && strcmp(registry->textures[i].path, path) == 0)
        {
            mel_vk_texture_shutdown(&registry->textures[i].texture, registry->vk);
            registry->textures[i].loaded = false;

            if (i < registry->texture_count - 1)
            {
                registry->textures[i] = registry->textures[registry->texture_count - 1];
            }
            registry->texture_count--;
            return true;
        }
    }

    return false;
}

u32 mel_asset_registry_texture_count(Mel_AssetRegistry* registry)
{
    assert(registry != nullptr);
    return registry->texture_count;
}

const char* mel_asset_registry_texture_path(Mel_AssetRegistry* registry, u32 index)
{
    assert(registry != nullptr);
    assert(index < registry->texture_count);
    return registry->textures[index].path;
}

Mel_TileVisual* mel_asset_registry_load_tile_visual(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    Mel_TileVisual* existing = mel_asset_registry_get_tile_visual(registry, path);
    if (existing) return existing;

    if (registry->tile_visual_count >= registry->tile_visual_capacity)
    {
        registry->tile_visual_capacity *= 2;
        registry->tile_visuals = mel_realloc(registry->alloc, registry->tile_visuals, registry->tile_visual_capacity * sizeof(Mel_TileVisualEntry));
    }

    Mel_TileVisualEntry* entry = &registry->tile_visuals[registry->tile_visual_count];
    strncpy(entry->path, path, sizeof(entry->path) - 1);

    if (!mel_tile_visual_load(&entry->tile_visual, registry->alloc, path))
    {
        return nullptr;
    }

    for (u32 i = 0; i < entry->tile_visual.source_count; i++)
    {
        Mel_TileSource* source = &entry->tile_visual.sources[i];
        if (source->path)
        {
            source->texture = mel_asset_registry_load_texture(registry, source->path);
            if (source->texture && source->is_sheet)
            {
                if (source->columns == 0) source->columns = source->texture->image.width / source->tile_width;
                if (source->rows == 0) source->rows = source->texture->image.height / source->tile_height;
            }
        }
    }

    entry->loaded = true;
    registry->tile_visual_count++;

    return &entry->tile_visual;
}

Mel_TileVisual* mel_asset_registry_get_tile_visual(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    for (u32 i = 0; i < registry->tile_visual_count; i++)
    {
        if (registry->tile_visuals[i].loaded && strcmp(registry->tile_visuals[i].path, path) == 0)
        {
            return &registry->tile_visuals[i].tile_visual;
        }
    }

    return nullptr;
}

Mel_TileVisual* mel_asset_registry_create_tile_visual(Mel_AssetRegistry* registry, const char* name)
{
    assert(registry != nullptr);
    assert(name != nullptr);

    if (registry->tile_visual_count >= registry->tile_visual_capacity)
    {
        registry->tile_visual_capacity *= 2;
        registry->tile_visuals = mel_realloc(registry->alloc, registry->tile_visuals, registry->tile_visual_capacity * sizeof(Mel_TileVisualEntry));
    }

    Mel_TileVisualEntry* entry = &registry->tile_visuals[registry->tile_visual_count];
    memset(entry, 0, sizeof(*entry));

    Mel_TileVisual* visual = &entry->tile_visual;
    visual->alloc = registry->alloc;
    size_t name_len = strlen(name) + 1;
    visual->name = mel_malloc(registry->alloc, name_len);
    memcpy((void*)visual->name, name, name_len);

    entry->loaded = true;
    registry->tile_visual_count++;

    return visual;
}

bool mel_asset_registry_save_tile_visual(Mel_AssetRegistry* registry, Mel_TileVisual* visual, const char* path)
{
    assert(registry != nullptr);
    assert(visual != nullptr);
    assert(path != nullptr);

    bool result = mel_tile_visual_save(visual, path);

    if (result)
    {
        for (u32 i = 0; i < registry->tile_visual_count; i++)
        {
            if (&registry->tile_visuals[i].tile_visual == visual)
            {
                strncpy(registry->tile_visuals[i].path, path, sizeof(registry->tile_visuals[i].path) - 1);
                break;
            }
        }
    }

    return result;
}

bool mel_asset_registry_unload_tile_visual(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    for (u32 i = 0; i < registry->tile_visual_count; i++)
    {
        if (registry->tile_visuals[i].loaded && strcmp(registry->tile_visuals[i].path, path) == 0)
        {
            mel_tile_visual_free(&registry->tile_visuals[i].tile_visual);
            registry->tile_visuals[i].loaded = false;

            if (i < registry->tile_visual_count - 1)
            {
                registry->tile_visuals[i] = registry->tile_visuals[registry->tile_visual_count - 1];
            }
            registry->tile_visual_count--;
            return true;
        }
    }

    return false;
}

u32 mel_asset_registry_tile_visual_count(Mel_AssetRegistry* registry)
{
    assert(registry != nullptr);
    return registry->tile_visual_count;
}

Mel_TileVisual* mel_asset_registry_tile_visual_at(Mel_AssetRegistry* registry, u32 index)
{
    assert(registry != nullptr);
    assert(index < registry->tile_visual_count);
    return &registry->tile_visuals[index].tile_visual;
}

const char* mel_asset_registry_tile_visual_path(Mel_AssetRegistry* registry, u32 index)
{
    assert(registry != nullptr);
    assert(index < registry->tile_visual_count);
    return registry->tile_visuals[index].path;
}

Mel_Tilemap* mel_asset_registry_load_tilemap(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    Mel_Tilemap* existing = mel_asset_registry_get_tilemap(registry, path);
    if (existing) return existing;

    if (registry->tilemap_count >= registry->tilemap_capacity)
    {
        registry->tilemap_capacity *= 2;
        registry->tilemaps = mel_realloc(registry->alloc, registry->tilemaps, registry->tilemap_capacity * sizeof(Mel_TilemapEntry));
    }

    Mel_TilemapEntry* entry = &registry->tilemaps[registry->tilemap_count];
    strncpy(entry->path, path, sizeof(entry->path) - 1);

    if (!mel_tilemap_load(&entry->tilemap, registry->alloc, path))
    {
        return nullptr;
    }

    if (entry->tilemap.tile_visual_path)
    {
        entry->tilemap.tile_visual = mel_asset_registry_load_tile_visual(registry, entry->tilemap.tile_visual_path);
    }

    entry->loaded = true;
    registry->tilemap_count++;

    return &entry->tilemap;
}

Mel_Tilemap* mel_asset_registry_get_tilemap(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    for (u32 i = 0; i < registry->tilemap_count; i++)
    {
        if (registry->tilemaps[i].loaded && strcmp(registry->tilemaps[i].path, path) == 0)
        {
            return &registry->tilemaps[i].tilemap;
        }
    }

    return nullptr;
}

Mel_Tilemap* mel_asset_registry_create_tilemap(Mel_AssetRegistry* registry, const char* name, u32 width, u32 height, u32 grid_width, u32 grid_height)
{
    assert(registry != nullptr);
    assert(name != nullptr);

    if (registry->tilemap_count >= registry->tilemap_capacity)
    {
        registry->tilemap_capacity *= 2;
        registry->tilemaps = mel_realloc(registry->alloc, registry->tilemaps, registry->tilemap_capacity * sizeof(Mel_TilemapEntry));
    }

    Mel_TilemapEntry* entry = &registry->tilemaps[registry->tilemap_count];
    memset(entry, 0, sizeof(*entry));

    Mel_Tilemap* tilemap = &entry->tilemap;
    tilemap->alloc = registry->alloc;
    size_t name_len = strlen(name) + 1;
    tilemap->name = mel_malloc(registry->alloc, name_len);
    memcpy((void*)tilemap->name, name, name_len);
    tilemap->width = width > 0 ? width : 20;
    tilemap->height = height > 0 ? height : 15;
    tilemap->grid_width = grid_width > 0 ? grid_width : 16;
    tilemap->grid_height = grid_height > 0 ? grid_height : 16;

    mel_tilemap_add_layer(tilemap, "Layer 1");

    entry->loaded = true;
    registry->tilemap_count++;

    return tilemap;
}

bool mel_asset_registry_save_tilemap(Mel_AssetRegistry* registry, Mel_Tilemap* tilemap, const char* path)
{
    assert(registry != nullptr);
    assert(tilemap != nullptr);
    assert(path != nullptr);

    bool result = mel_tilemap_save(tilemap, path);

    if (result)
    {
        for (u32 i = 0; i < registry->tilemap_count; i++)
        {
            if (&registry->tilemaps[i].tilemap == tilemap)
            {
                strncpy(registry->tilemaps[i].path, path, sizeof(registry->tilemaps[i].path) - 1);
                break;
            }
        }
    }

    return result;
}

bool mel_asset_registry_unload_tilemap(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    for (u32 i = 0; i < registry->tilemap_count; i++)
    {
        if (registry->tilemaps[i].loaded && strcmp(registry->tilemaps[i].path, path) == 0)
        {
            mel_tilemap_free(&registry->tilemaps[i].tilemap);
            registry->tilemaps[i].loaded = false;

            if (i < registry->tilemap_count - 1)
            {
                registry->tilemaps[i] = registry->tilemaps[registry->tilemap_count - 1];
            }
            registry->tilemap_count--;
            return true;
        }
    }

    return false;
}

u32 mel_asset_registry_tilemap_count(Mel_AssetRegistry* registry)
{
    assert(registry != nullptr);
    return registry->tilemap_count;
}

Mel_Tilemap* mel_asset_registry_tilemap_at(Mel_AssetRegistry* registry, u32 index)
{
    assert(registry != nullptr);
    assert(index < registry->tilemap_count);
    return &registry->tilemaps[index].tilemap;
}

const char* mel_asset_registry_tilemap_path(Mel_AssetRegistry* registry, u32 index)
{
    assert(registry != nullptr);
    assert(index < registry->tilemap_count);
    return registry->tilemaps[index].path;
}

Mel_Spritesheet* mel_asset_registry_load_spritesheet(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    Mel_Spritesheet* existing = mel_asset_registry_get_spritesheet(registry, path);
    if (existing) return existing;

    if (registry->spritesheet_count >= registry->spritesheet_capacity)
    {
        registry->spritesheet_capacity *= 2;
        registry->spritesheets = mel_realloc(registry->alloc, registry->spritesheets, registry->spritesheet_capacity * sizeof(Mel_SpritesheetEntry));
    }

    Mel_SpritesheetEntry* entry = &registry->spritesheets[registry->spritesheet_count];
    strncpy(entry->path, path, sizeof(entry->path) - 1);

    if (!mel_spritesheet_load(&entry->spritesheet, registry->alloc, path))
    {
        return nullptr;
    }

    if (entry->spritesheet.texture_path)
    {
        entry->spritesheet.texture = mel_asset_registry_load_texture(registry, entry->spritesheet.texture_path);
    }

    entry->loaded = true;
    registry->spritesheet_count++;

    return &entry->spritesheet;
}

Mel_Spritesheet* mel_asset_registry_get_spritesheet(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    for (u32 i = 0; i < registry->spritesheet_count; i++)
    {
        if (registry->spritesheets[i].loaded && strcmp(registry->spritesheets[i].path, path) == 0)
        {
            return &registry->spritesheets[i].spritesheet;
        }
    }

    return nullptr;
}

Mel_Spritesheet* mel_asset_registry_create_spritesheet(Mel_AssetRegistry* registry, const char* name, const char* texture_path)
{
    assert(registry != nullptr);
    assert(name != nullptr);
    assert(texture_path != nullptr);

    if (registry->spritesheet_count >= registry->spritesheet_capacity)
    {
        registry->spritesheet_capacity *= 2;
        registry->spritesheets = mel_realloc(registry->alloc, registry->spritesheets, registry->spritesheet_capacity * sizeof(Mel_SpritesheetEntry));
    }

    Mel_SpritesheetEntry* entry = &registry->spritesheets[registry->spritesheet_count];
    memset(entry, 0, sizeof(*entry));

    Mel_Spritesheet* sheet = &entry->spritesheet;
    sheet->alloc = registry->alloc;
    size_t name_len = strlen(name) + 1;
    sheet->name = mel_malloc(registry->alloc, name_len);
    memcpy((void*)sheet->name, name, name_len);
    size_t texture_path_len = strlen(texture_path) + 1;
    sheet->texture_path = mel_malloc(registry->alloc, texture_path_len);
    memcpy((void*)sheet->texture_path, texture_path, texture_path_len);

    sheet->texture = mel_asset_registry_load_texture(registry, texture_path);
    if (!sheet->texture)
    {
        mel_free(registry->alloc, (void*)sheet->name);
        mel_free(registry->alloc, (void*)sheet->texture_path);
        return nullptr;
    }

    sheet->texture_width = sheet->texture->image.width;
    sheet->texture_height = sheet->texture->image.height;

    entry->loaded = true;
    registry->spritesheet_count++;

    return sheet;
}

bool mel_asset_registry_save_spritesheet(Mel_AssetRegistry* registry, Mel_Spritesheet* sheet, const char* path)
{
    assert(registry != nullptr);
    assert(sheet != nullptr);
    assert(path != nullptr);

    return mel_spritesheet_save(sheet, path);
}

bool mel_asset_registry_unload_spritesheet(Mel_AssetRegistry* registry, const char* path)
{
    assert(registry != nullptr);
    assert(path != nullptr);

    for (u32 i = 0; i < registry->spritesheet_count; i++)
    {
        if (registry->spritesheets[i].loaded && strcmp(registry->spritesheets[i].path, path) == 0)
        {
            mel_spritesheet_free(&registry->spritesheets[i].spritesheet);
            registry->spritesheets[i].loaded = false;

            if (i < registry->spritesheet_count - 1)
            {
                registry->spritesheets[i] = registry->spritesheets[registry->spritesheet_count - 1];
            }
            registry->spritesheet_count--;
            return true;
        }
    }

    return false;
}

u32 mel_asset_registry_spritesheet_count(Mel_AssetRegistry* registry)
{
    assert(registry != nullptr);
    return registry->spritesheet_count;
}

Mel_Spritesheet* mel_asset_registry_spritesheet_at(Mel_AssetRegistry* registry, u32 index)
{
    assert(registry != nullptr);
    assert(index < registry->spritesheet_count);
    return &registry->spritesheets[index].spritesheet;
}

void mel_asset_registry_scan_directory(Mel_AssetRegistry* registry, const char* dir)
{
    assert(registry != nullptr);

    const char* scan_dir = dir ? dir : "";

    u32 file_count = 0;
    char** files = mel_assets_list(scan_dir, &file_count);
    if (!files)
    {
        SDL_Log("No files found in '%s'", scan_dir);
        return;
    }

    for (u32 i = 0; i < file_count; i++)
    {
        const char* filename = files[i];
        size_t len = strlen(filename);

        char full_path[512];
        if (strlen(scan_dir) > 0)
        {
            snprintf(full_path, sizeof(full_path), "%s/%s", scan_dir, filename);
        }
        else
        {
            strncpy(full_path, filename, sizeof(full_path) - 1);
        }

        if (mel_assets_is_directory(full_path))
        {
            mel_asset_registry_scan_directory(registry, full_path);
            continue;
        }

        if (len > 4 && strcmp(filename + len - 4, ".png") == 0)
        {
            mel_asset_registry_load_texture(registry, full_path);
        }
        else if (len > 4 && strcmp(filename + len - 4, ".jpg") == 0)
        {
            mel_asset_registry_load_texture(registry, full_path);
        }
        else if (len > 5 && strcmp(filename + len - 5, ".jpeg") == 0)
        {
            mel_asset_registry_load_texture(registry, full_path);
        }
        else if (len > 5 && strcmp(filename + len - 5, ".json") == 0)
        {
            char* json_text = mel_assets_read_text(full_path);
            if (json_text)
            {
                bool has_sources = strstr(json_text, "\"sources\"") != nullptr;
                bool has_tiles = strstr(json_text, "\"tiles\"") != nullptr;
                bool has_layers = strstr(json_text, "\"layers\"") != nullptr;
                bool has_grid_width = strstr(json_text, "\"grid_width\"") != nullptr;
                bool has_animations = strstr(json_text, "\"animations\"") != nullptr;
                bool has_frames = strstr(json_text, "\"frames\"") != nullptr;
                bool has_texture_path = strstr(json_text, "\"texture_path\"") != nullptr;

                if (has_sources && has_tiles && !has_layers)
                {
                    SDL_Log("Detected tile visual: %s", full_path);
                    mel_asset_registry_load_tile_visual(registry, full_path);
                }
                else if (has_layers && has_grid_width)
                {
                    SDL_Log("Detected tilemap: %s", full_path);
                    mel_asset_registry_load_tilemap(registry, full_path);
                }
                else if ((has_animations || has_frames) && has_texture_path)
                {
                    SDL_Log("Detected spritesheet: %s", full_path);
                    mel_asset_registry_load_spritesheet(registry, full_path);
                }
                mel_assets_free(json_text);
            }
        }
    }

    mel_assets_list_free(files);
    SDL_Log("Scanned directory '%s': found %u files", scan_dir, file_count);
}
