#include "tilemap.h"
#include "assets.h"
#include "memory.h"
#include <cjson/cJSON.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

bool mel_tilemap_load(Mel_Tilemap* tilemap, const Mel_Alloc* alloc, const char* path)
{
    assert(tilemap != nullptr);
    assert(alloc != nullptr);
    assert(path != nullptr);

    char* json_text = mel_assets_read_text(path);
    if (!json_text)
    {
        SDL_Log("Failed to read tilemap: %s", path);
        return false;
    }

    cJSON* root = cJSON_Parse(json_text);
    mel_assets_free(json_text);

    if (!root)
    {
        SDL_Log("Failed to parse tilemap JSON: %s", path);
        return false;
    }

    *tilemap = (Mel_Tilemap){0};
    tilemap->alloc = alloc;

    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name))
    {
        size_t len = strlen(name->valuestring) + 1;
        tilemap->name = (const char*)mel_malloc(alloc, len);
        memcpy((void*)tilemap->name, name->valuestring, len);
    }

    cJSON* tile_visual = cJSON_GetObjectItem(root, "tile_visual");
    if (tile_visual && cJSON_IsString(tile_visual))
    {
        size_t len = strlen(tile_visual->valuestring) + 1;
        tilemap->tile_visual_path = (const char*)mel_malloc(alloc, len);
        memcpy((void*)tilemap->tile_visual_path, tile_visual->valuestring, len);
    }

    cJSON* width = cJSON_GetObjectItem(root, "width");
    if (width && cJSON_IsNumber(width))
    {
        tilemap->width = (u32)width->valueint;
    }

    cJSON* height = cJSON_GetObjectItem(root, "height");
    if (height && cJSON_IsNumber(height))
    {
        tilemap->height = (u32)height->valueint;
    }

    cJSON* grid_width = cJSON_GetObjectItem(root, "grid_width");
    if (grid_width && cJSON_IsNumber(grid_width))
    {
        tilemap->grid_width = (u32)grid_width->valueint;
    }
    else
    {
        tilemap->grid_width = 16;
    }

    cJSON* grid_height = cJSON_GetObjectItem(root, "grid_height");
    if (grid_height && cJSON_IsNumber(grid_height))
    {
        tilemap->grid_height = (u32)grid_height->valueint;
    }
    else
    {
        tilemap->grid_height = 16;
    }

    cJSON* layers = cJSON_GetObjectItem(root, "layers");
    if (layers && cJSON_IsArray(layers))
    {
        tilemap->layer_count = (u32)cJSON_GetArraySize(layers);
        tilemap->layers = (Mel_TilemapLayer*)mel_calloc(alloc, tilemap->layer_count * sizeof(Mel_TilemapLayer));

        u32 i = 0;
        cJSON* layer;
        cJSON_ArrayForEach(layer, layers)
        {
            Mel_TilemapLayer* l = &tilemap->layers[i];

            cJSON* layer_name = cJSON_GetObjectItem(layer, "name");
            if (layer_name && cJSON_IsString(layer_name))
            {
                size_t len = strlen(layer_name->valuestring) + 1;
                l->name = (const char*)mel_malloc(alloc, len);
                memcpy((void*)l->name, layer_name->valuestring, len);
            }

            cJSON* visible = cJSON_GetObjectItem(layer, "visible");
            l->visible = !visible || cJSON_IsTrue(visible);

            cJSON* parallax_x = cJSON_GetObjectItem(layer, "parallax_x");
            l->parallax_x = (parallax_x && cJSON_IsNumber(parallax_x)) ? (f32)parallax_x->valuedouble : 1.0f;

            cJSON* parallax_y = cJSON_GetObjectItem(layer, "parallax_y");
            l->parallax_y = (parallax_y && cJSON_IsNumber(parallax_y)) ? (f32)parallax_y->valuedouble : 1.0f;

            cJSON* offset_x = cJSON_GetObjectItem(layer, "offset_x");
            l->offset_x = (offset_x && cJSON_IsNumber(offset_x)) ? (i32)offset_x->valueint : 0;

            cJSON* offset_y = cJSON_GetObjectItem(layer, "offset_y");
            l->offset_y = (offset_y && cJSON_IsNumber(offset_y)) ? (i32)offset_y->valueint : 0;

            l->width = tilemap->width;
            l->height = tilemap->height;

            u32 tile_count = tilemap->width * tilemap->height;
            l->tiles = (i32*)mel_malloc(alloc, tile_count * sizeof(i32));

            for (u32 t = 0; t < tile_count; t++)
            {
                l->tiles[t] = -1;
            }

            cJSON* data = cJSON_GetObjectItem(layer, "data");
            if (data && cJSON_IsArray(data))
            {
                u32 j = 0;
                cJSON* tile;
                cJSON_ArrayForEach(tile, data)
                {
                    if (j < tile_count && cJSON_IsNumber(tile))
                    {
                        l->tiles[j] = (i32)tile->valueint;
                    }
                    j++;
                }
            }

            i++;
        }
    }

    cJSON_Delete(root);

    SDL_Log("Loaded tilemap: %s (%ux%u, %u layers)",
            tilemap->name ? tilemap->name : path,
            tilemap->width, tilemap->height, tilemap->layer_count);

    return true;
}

bool mel_tilemap_save(Mel_Tilemap* tilemap, const char* path)
{
    assert(tilemap != nullptr);
    assert(path != nullptr);

    cJSON* root = cJSON_CreateObject();

    if (tilemap->name)
        cJSON_AddStringToObject(root, "name", tilemap->name);
    if (tilemap->tile_visual_path)
        cJSON_AddStringToObject(root, "tile_visual", tilemap->tile_visual_path);

    cJSON_AddNumberToObject(root, "width", tilemap->width);
    cJSON_AddNumberToObject(root, "height", tilemap->height);
    cJSON_AddNumberToObject(root, "grid_width", tilemap->grid_width);
    cJSON_AddNumberToObject(root, "grid_height", tilemap->grid_height);

    if (tilemap->layer_count > 0)
    {
        cJSON* layers = cJSON_AddArrayToObject(root, "layers");
        for (u32 i = 0; i < tilemap->layer_count; i++)
        {
            Mel_TilemapLayer* layer = &tilemap->layers[i];
            cJSON* layer_obj = cJSON_CreateObject();

            if (layer->name)
                cJSON_AddStringToObject(layer_obj, "name", layer->name);
            cJSON_AddBoolToObject(layer_obj, "visible", layer->visible);

            if (layer->parallax_x != 1.0f)
                cJSON_AddNumberToObject(layer_obj, "parallax_x", layer->parallax_x);
            if (layer->parallax_y != 1.0f)
                cJSON_AddNumberToObject(layer_obj, "parallax_y", layer->parallax_y);
            if (layer->offset_x != 0)
                cJSON_AddNumberToObject(layer_obj, "offset_x", layer->offset_x);
            if (layer->offset_y != 0)
                cJSON_AddNumberToObject(layer_obj, "offset_y", layer->offset_y);

            cJSON* data = cJSON_AddArrayToObject(layer_obj, "data");
            u32 tile_count = layer->width * layer->height;
            for (u32 j = 0; j < tile_count; j++)
            {
                cJSON_AddItemToArray(data, cJSON_CreateNumber(layer->tiles[j]));
            }

            cJSON_AddItemToArray(layers, layer_obj);
        }
    }

    char* json_text = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_text)
    {
        SDL_Log("Failed to serialize tilemap JSON");
        return false;
    }

    bool result = mel_assets_write_text(path, json_text);
    free(json_text);

    if (result)
    {
        SDL_Log("Saved tilemap: %s", path);
    }

    return result;
}

void mel_tilemap_free(Mel_Tilemap* tilemap)
{
    assert(tilemap != nullptr);

    if (tilemap->name)
        mel_free(tilemap->alloc, (void*)tilemap->name);
    if (tilemap->tile_visual_path)
        mel_free(tilemap->alloc, (void*)tilemap->tile_visual_path);

    if (tilemap->layers)
    {
        for (u32 i = 0; i < tilemap->layer_count; i++)
        {
            if (tilemap->layers[i].name)
                mel_free(tilemap->alloc, (void*)tilemap->layers[i].name);
            if (tilemap->layers[i].tiles)
                mel_free(tilemap->alloc, tilemap->layers[i].tiles);
        }
        mel_free(tilemap->alloc, tilemap->layers);
    }

    *tilemap = (Mel_Tilemap){0};
}

i32 mel_tilemap_get_tile(Mel_Tilemap* tilemap, u32 layer, u32 x, u32 y)
{
    assert(tilemap != nullptr);
    assert(layer < tilemap->layer_count);
    assert(x < tilemap->width);
    assert(y < tilemap->height);

    return tilemap->layers[layer].tiles[y * tilemap->width + x];
}

void mel_tilemap_set_tile(Mel_Tilemap* tilemap, u32 layer, u32 x, u32 y, i32 tile)
{
    assert(tilemap != nullptr);
    assert(layer < tilemap->layer_count);
    assert(x < tilemap->width);
    assert(y < tilemap->height);

    tilemap->layers[layer].tiles[y * tilemap->width + x] = tile;
}

Mel_TilemapLayer* mel_tilemap_add_layer(Mel_Tilemap* tilemap, const char* name)
{
    assert(tilemap != nullptr);

    u32 new_count = tilemap->layer_count + 1;
    Mel_TilemapLayer* new_layers;
    if (tilemap->layers)
        new_layers = (Mel_TilemapLayer*)mel_realloc(tilemap->alloc, tilemap->layers, new_count * sizeof(Mel_TilemapLayer));
    else
        new_layers = (Mel_TilemapLayer*)mel_malloc(tilemap->alloc, new_count * sizeof(Mel_TilemapLayer));

    if (!new_layers) return nullptr;

    tilemap->layers = new_layers;
    Mel_TilemapLayer* layer = &tilemap->layers[tilemap->layer_count];
    tilemap->layer_count = new_count;

    *layer = (Mel_TilemapLayer){0};
    if (name)
    {
        size_t len = strlen(name) + 1;
        layer->name = (const char*)mel_malloc(tilemap->alloc, len);
        memcpy((void*)layer->name, name, len);
    }
    else
    {
        layer->name = nullptr;
    }
    layer->width = tilemap->width;
    layer->height = tilemap->height;
    layer->visible = true;
    layer->parallax_x = 1.0f;
    layer->parallax_y = 1.0f;

    u32 tile_count = tilemap->width * tilemap->height;
    layer->tiles = (i32*)mel_malloc(tilemap->alloc, tile_count * sizeof(i32));
    for (u32 i = 0; i < tile_count; i++)
    {
        layer->tiles[i] = -1;
    }

    return layer;
}

bool mel_tilemap_remove_layer(Mel_Tilemap* tilemap, u32 layer_idx)
{
    assert(tilemap != nullptr);

    if (layer_idx >= tilemap->layer_count) return false;

    Mel_TilemapLayer* layer = &tilemap->layers[layer_idx];
    if (layer->name)
        mel_free(tilemap->alloc, (void*)layer->name);
    if (layer->tiles)
        mel_free(tilemap->alloc, layer->tiles);

    for (u32 i = layer_idx; i < tilemap->layer_count - 1; i++)
    {
        tilemap->layers[i] = tilemap->layers[i + 1];
    }

    tilemap->layer_count--;
    return true;
}

bool mel_tilemap_move_layer(Mel_Tilemap* tilemap, u32 from_idx, u32 to_idx)
{
    assert(tilemap != nullptr);

    if (from_idx >= tilemap->layer_count || to_idx >= tilemap->layer_count)
        return false;

    if (from_idx == to_idx)
        return true;

    Mel_TilemapLayer temp = tilemap->layers[from_idx];

    if (from_idx < to_idx)
    {
        for (u32 i = from_idx; i < to_idx; i++)
        {
            tilemap->layers[i] = tilemap->layers[i + 1];
        }
    }
    else
    {
        for (u32 i = from_idx; i > to_idx; i--)
        {
            tilemap->layers[i] = tilemap->layers[i - 1];
        }
    }

    tilemap->layers[to_idx] = temp;
    return true;
}

bool mel_tilemap_resize(Mel_Tilemap* tilemap, u32 new_width, u32 new_height)
{
    assert(tilemap != nullptr);

    if (new_width == 0 || new_height == 0) return false;
    if (new_width == tilemap->width && new_height == tilemap->height) return true;

    for (u32 l = 0; l < tilemap->layer_count; l++)
    {
        Mel_TilemapLayer* layer = &tilemap->layers[l];

        i32* new_tiles = (i32*)mel_malloc(tilemap->alloc, new_width * new_height * sizeof(i32));
        if (!new_tiles) return false;

        for (u32 i = 0; i < new_width * new_height; i++)
        {
            new_tiles[i] = -1;
        }

        u32 copy_width = (tilemap->width < new_width) ? tilemap->width : new_width;
        u32 copy_height = (tilemap->height < new_height) ? tilemap->height : new_height;

        for (u32 y = 0; y < copy_height; y++)
        {
            for (u32 x = 0; x < copy_width; x++)
            {
                new_tiles[y * new_width + x] = layer->tiles[y * tilemap->width + x];
            }
        }

        mel_free(tilemap->alloc, layer->tiles);
        layer->tiles = new_tiles;
        layer->width = new_width;
        layer->height = new_height;
    }

    tilemap->width = new_width;
    tilemap->height = new_height;

    return true;
}
