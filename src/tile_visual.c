#include "tile_visual.h"
#include "assets.h"
#include "memory.h"
#include <cjson/cJSON.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

bool mel_tile_visual_load(Mel_TileVisual* visual, const Mel_Alloc* alloc, const char* path)
{
    assert(visual != nullptr);
    assert(alloc != nullptr);
    assert(path != nullptr);

    char* json_text = mel_assets_read_text(path);
    if (!json_text)
    {
        SDL_Log("Failed to read tile visual: %s", path);
        return false;
    }

    cJSON* root = cJSON_Parse(json_text);
    mel_assets_free(json_text);

    if (!root)
    {
        SDL_Log("Failed to parse tile visual JSON: %s", path);
        return false;
    }

    *visual = (Mel_TileVisual){0};
    visual->alloc = alloc;

    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name))
    {
        usize len = strlen(name->valuestring);
        visual->name = (const char*)mel_malloc(alloc, len + 1);
        memcpy((void*)visual->name, name->valuestring, len + 1);
    }

    cJSON* sources = cJSON_GetObjectItem(root, "sources");
    if (sources && cJSON_IsArray(sources))
    {
        visual->source_count = (u32)cJSON_GetArraySize(sources);
        visual->sources = (Mel_TileSource*)mel_calloc(alloc, visual->source_count * sizeof(Mel_TileSource));

        u32 i = 0;
        cJSON* src;
        cJSON_ArrayForEach(src, sources)
        {
            Mel_TileSource* source = &visual->sources[i];

            cJSON* src_path = cJSON_GetObjectItem(src, "path");
            if (src_path && cJSON_IsString(src_path))
            {
                usize len = strlen(src_path->valuestring);
                source->path = (const char*)mel_malloc(alloc, len + 1);
                memcpy((void*)source->path, src_path->valuestring, len + 1);
            }

            cJSON* is_sheet = cJSON_GetObjectItem(src, "is_sheet");
            source->is_sheet = is_sheet && cJSON_IsTrue(is_sheet);

            cJSON* tile_width = cJSON_GetObjectItem(src, "tile_width");
            if (tile_width && cJSON_IsNumber(tile_width))
            {
                source->tile_width = (u32)tile_width->valueint;
            }

            cJSON* tile_height = cJSON_GetObjectItem(src, "tile_height");
            if (tile_height && cJSON_IsNumber(tile_height))
            {
                source->tile_height = (u32)tile_height->valueint;
            }

            cJSON* columns = cJSON_GetObjectItem(src, "columns");
            if (columns && cJSON_IsNumber(columns))
            {
                source->columns = (u32)columns->valueint;
            }

            cJSON* rows = cJSON_GetObjectItem(src, "rows");
            if (rows && cJSON_IsNumber(rows))
            {
                source->rows = (u32)rows->valueint;
            }

            cJSON* padding = cJSON_GetObjectItem(src, "padding");
            if (padding && cJSON_IsNumber(padding))
            {
                source->padding = (u32)padding->valueint;
            }

            cJSON* margin = cJSON_GetObjectItem(src, "margin");
            if (margin && cJSON_IsNumber(margin))
            {
                source->margin = (u32)margin->valueint;
            }

            i++;
        }
    }

    cJSON* tiles = cJSON_GetObjectItem(root, "tiles");
    if (tiles && cJSON_IsArray(tiles))
    {
        visual->tile_count = (u32)cJSON_GetArraySize(tiles);
        visual->tiles = (Mel_TileEntry*)mel_calloc(alloc, visual->tile_count * sizeof(Mel_TileEntry));

        u32 i = 0;
        cJSON* tile;
        cJSON_ArrayForEach(tile, tiles)
        {
            Mel_TileEntry* entry = &visual->tiles[i];

            cJSON* id = cJSON_GetObjectItem(tile, "id");
            if (id && cJSON_IsNumber(id))
            {
                entry->id = (u32)id->valueint;
            }

            cJSON* source = cJSON_GetObjectItem(tile, "source");
            if (source && cJSON_IsNumber(source))
            {
                entry->source_idx = (u32)source->valueint;
            }

            cJSON* x = cJSON_GetObjectItem(tile, "x");
            if (x && cJSON_IsNumber(x))
            {
                entry->source_x = (u32)x->valueint;
            }

            cJSON* y = cJSON_GetObjectItem(tile, "y");
            if (y && cJSON_IsNumber(y))
            {
                entry->source_y = (u32)y->valueint;
            }

            cJSON* width = cJSON_GetObjectItem(tile, "width");
            if (width && cJSON_IsNumber(width))
            {
                entry->width = (u32)width->valueint;
            }

            cJSON* height = cJSON_GetObjectItem(tile, "height");
            if (height && cJSON_IsNumber(height))
            {
                entry->height = (u32)height->valueint;
            }

            cJSON* flags = cJSON_GetObjectItem(tile, "flags");
            if (flags && cJSON_IsNumber(flags))
            {
                entry->flags = (u32)flags->valueint;
            }

            cJSON* tags = cJSON_GetObjectItem(tile, "tags");
            if (tags && cJSON_IsArray(tags))
            {
                entry->tag_count = (u32)cJSON_GetArraySize(tags);
                if (entry->tag_count > 0)
                {
                    entry->tags = (const char**)mel_calloc(alloc, entry->tag_count * sizeof(const char*));
                    u32 j = 0;
                    cJSON* tag;
                    cJSON_ArrayForEach(tag, tags)
                    {
                        if (cJSON_IsString(tag))
                        {
                            usize len = strlen(tag->valuestring);
                            entry->tags[j] = (const char*)mel_malloc(alloc, len + 1);
                            memcpy((void*)entry->tags[j], tag->valuestring, len + 1);
                        }
                        j++;
                    }
                }
            }

            i++;
        }
    }

    cJSON_Delete(root);

    SDL_Log("Loaded tile visual: %s (%u sources, %u tiles)",
            visual->name ? visual->name : path,
            visual->source_count, visual->tile_count);

    return true;
}

bool mel_tile_visual_save(Mel_TileVisual* visual, const char* path)
{
    assert(visual != nullptr);
    assert(path != nullptr);

    cJSON* root = cJSON_CreateObject();

    if (visual->name)
        cJSON_AddStringToObject(root, "name", visual->name);

    if (visual->source_count > 0)
    {
        cJSON* sources = cJSON_AddArrayToObject(root, "sources");
        for (u32 i = 0; i < visual->source_count; i++)
        {
            Mel_TileSource* source = &visual->sources[i];
            cJSON* src = cJSON_CreateObject();

            if (source->path)
                cJSON_AddStringToObject(src, "path", source->path);
            cJSON_AddBoolToObject(src, "is_sheet", source->is_sheet);

            if (source->is_sheet)
            {
                cJSON_AddNumberToObject(src, "tile_width", source->tile_width);
                cJSON_AddNumberToObject(src, "tile_height", source->tile_height);
                cJSON_AddNumberToObject(src, "columns", source->columns);
                cJSON_AddNumberToObject(src, "rows", source->rows);
                cJSON_AddNumberToObject(src, "padding", source->padding);
                cJSON_AddNumberToObject(src, "margin", source->margin);
            }

            cJSON_AddItemToArray(sources, src);
        }
    }

    if (visual->tile_count > 0)
    {
        cJSON* tiles = cJSON_AddArrayToObject(root, "tiles");
        for (u32 i = 0; i < visual->tile_count; i++)
        {
            Mel_TileEntry* entry = &visual->tiles[i];
            cJSON* tile = cJSON_CreateObject();

            cJSON_AddNumberToObject(tile, "id", entry->id);
            cJSON_AddNumberToObject(tile, "source", entry->source_idx);
            cJSON_AddNumberToObject(tile, "x", entry->source_x);
            cJSON_AddNumberToObject(tile, "y", entry->source_y);
            cJSON_AddNumberToObject(tile, "width", entry->width);
            cJSON_AddNumberToObject(tile, "height", entry->height);

            if (entry->flags != 0)
            {
                cJSON_AddNumberToObject(tile, "flags", entry->flags);
            }

            if (entry->tag_count > 0)
            {
                cJSON* tags = cJSON_AddArrayToObject(tile, "tags");
                for (u32 j = 0; j < entry->tag_count; j++)
                {
                    if (entry->tags[j])
                    {
                        cJSON_AddItemToArray(tags, cJSON_CreateString(entry->tags[j]));
                    }
                }
            }

            cJSON_AddItemToArray(tiles, tile);
        }
    }

    char* json_text = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_text)
    {
        SDL_Log("Failed to serialize tile visual JSON");
        return false;
    }

    bool result = mel_assets_write_text(path, json_text);
    free(json_text);

    if (result)
    {
        SDL_Log("Saved tile visual: %s", path);
    }

    return result;
}

void mel_tile_visual_free(Mel_TileVisual* visual)
{
    assert(visual != nullptr);

    if (visual->name)
        mel_free(visual->alloc, (void*)visual->name);

    if (visual->sources)
    {
        for (u32 i = 0; i < visual->source_count; i++)
        {
            if (visual->sources[i].path)
                mel_free(visual->alloc, (void*)visual->sources[i].path);
        }
        mel_free(visual->alloc, visual->sources);
    }

    if (visual->tiles)
    {
        for (u32 i = 0; i < visual->tile_count; i++)
        {
            if (visual->tiles[i].tags)
            {
                for (u32 j = 0; j < visual->tiles[i].tag_count; j++)
                {
                    if (visual->tiles[i].tags[j])
                        mel_free(visual->alloc, (void*)visual->tiles[i].tags[j]);
                }
                mel_free(visual->alloc, visual->tiles[i].tags);
            }
        }
        mel_free(visual->alloc, visual->tiles);
    }

    *visual = (Mel_TileVisual){0};
}

Mel_TileSource* mel_tile_visual_add_source(Mel_TileVisual* visual, const char* texture_path, bool is_sheet)
{
    assert(visual != nullptr);
    assert(texture_path != nullptr);

    u32 new_count = visual->source_count + 1;
    Mel_TileSource* new_sources = (Mel_TileSource*)mel_realloc(
        visual->alloc, visual->sources, new_count * sizeof(Mel_TileSource));

    if (!new_sources) return nullptr;

    visual->sources = new_sources;
    Mel_TileSource* source = &visual->sources[visual->source_count];
    visual->source_count = new_count;

    *source = (Mel_TileSource){0};
    usize len = strlen(texture_path);
    source->path = (const char*)mel_malloc(visual->alloc, len + 1);
    memcpy((void*)source->path, texture_path, len + 1);
    source->is_sheet = is_sheet;
    source->tile_width = 16;
    source->tile_height = 16;
    source->columns = 1;
    source->rows = 1;

    return source;
}

bool mel_tile_visual_remove_source(Mel_TileVisual* visual, u32 source_idx)
{
    assert(visual != nullptr);

    if (source_idx >= visual->source_count) return false;

    if (visual->sources[source_idx].path)
        mel_free(visual->alloc, (void*)visual->sources[source_idx].path);

    for (u32 i = source_idx; i < visual->source_count - 1; i++)
    {
        visual->sources[i] = visual->sources[i + 1];
    }

    visual->source_count--;

    u32 new_tile_count = 0;
    for (u32 i = 0; i < visual->tile_count; i++)
    {
        if (visual->tiles[i].source_idx != source_idx)
        {
            if (visual->tiles[i].source_idx > source_idx)
            {
                visual->tiles[i].source_idx--;
            }
            if (new_tile_count != i)
            {
                visual->tiles[new_tile_count] = visual->tiles[i];
            }
            new_tile_count++;
        }
        else
        {
            if (visual->tiles[i].tags)
            {
                for (u32 j = 0; j < visual->tiles[i].tag_count; j++)
                {
                    if (visual->tiles[i].tags[j])
                        mel_free(visual->alloc, (void*)visual->tiles[i].tags[j]);
                }
                mel_free(visual->alloc, visual->tiles[i].tags);
            }
        }
    }
    visual->tile_count = new_tile_count;

    return true;
}

void mel_tile_visual_regenerate_tiles(Mel_TileVisual* visual)
{
    assert(visual != nullptr);

    if (visual->tiles)
    {
        for (u32 i = 0; i < visual->tile_count; i++)
        {
            if (visual->tiles[i].tags)
            {
                for (u32 j = 0; j < visual->tiles[i].tag_count; j++)
                {
                    if (visual->tiles[i].tags[j])
                        mel_free(visual->alloc, (void*)visual->tiles[i].tags[j]);
                }
                mel_free(visual->alloc, visual->tiles[i].tags);
            }
        }
        mel_free(visual->alloc, visual->tiles);
        visual->tiles = nullptr;
        visual->tile_count = 0;
    }

    u32 total_tiles = 0;
    for (u32 s = 0; s < visual->source_count; s++)
    {
        Mel_TileSource* source = &visual->sources[s];
        if (source->is_sheet)
        {
            total_tiles += source->columns * source->rows;
        }
        else
        {
            total_tiles += 1;
        }
    }

    if (total_tiles == 0) return;

    visual->tiles = (Mel_TileEntry*)mel_calloc(visual->alloc, total_tiles * sizeof(Mel_TileEntry));
    visual->tile_count = total_tiles;

    u32 tile_idx = 0;
    for (u32 s = 0; s < visual->source_count; s++)
    {
        Mel_TileSource* source = &visual->sources[s];
        if (source->is_sheet)
        {
            for (u32 y = 0; y < source->rows; y++)
            {
                for (u32 x = 0; x < source->columns; x++)
                {
                    Mel_TileEntry* entry = &visual->tiles[tile_idx];
                    entry->id = tile_idx;
                    entry->source_idx = s;
                    entry->source_x = source->margin + x * (source->tile_width + source->padding);
                    entry->source_y = source->margin + y * (source->tile_height + source->padding);
                    entry->width = source->tile_width;
                    entry->height = source->tile_height;
                    tile_idx++;
                }
            }
        }
        else
        {
            Mel_TileEntry* entry = &visual->tiles[tile_idx];
            entry->id = tile_idx;
            entry->source_idx = s;
            entry->source_x = 0;
            entry->source_y = 0;
            if (source->texture)
            {
                entry->width = source->texture->image.width;
                entry->height = source->texture->image.height;
            }
            tile_idx++;
        }
    }
}

Mel_TileEntry* mel_tile_visual_get_tile(Mel_TileVisual* visual, u32 tile_id)
{
    assert(visual != nullptr);

    for (u32 i = 0; i < visual->tile_count; i++)
    {
        if (visual->tiles[i].id == tile_id)
        {
            return &visual->tiles[i];
        }
    }

    return nullptr;
}

void mel_tile_visual_get_tile_uv(Mel_TileVisual* visual, u32 tile_id, f32* u0, f32* v0, f32* u1, f32* v1)
{
    assert(visual != nullptr);

    Mel_TileEntry* entry = mel_tile_visual_get_tile(visual, tile_id);
    if (!entry || entry->source_idx >= visual->source_count)
    {
        *u0 = *v0 = 0.0f;
        *u1 = *v1 = 1.0f;
        return;
    }

    Mel_TileSource* source = &visual->sources[entry->source_idx];
    if (!source->texture)
    {
        *u0 = *v0 = 0.0f;
        *u1 = *v1 = 1.0f;
        return;
    }

    f32 tex_width = (f32)source->texture->image.width;
    f32 tex_height = (f32)source->texture->image.height;

    *u0 = (f32)entry->source_x / tex_width;
    *v0 = (f32)entry->source_y / tex_height;
    *u1 = (f32)(entry->source_x + entry->width) / tex_width;
    *v1 = (f32)(entry->source_y + entry->height) / tex_height;
}

bool mel_tile_visual_tile_has_tag(Mel_TileVisual* visual, u32 tile_id, const char* tag)
{
    assert(visual != nullptr);
    assert(tag != nullptr);

    Mel_TileEntry* entry = mel_tile_visual_get_tile(visual, tile_id);
    if (!entry) return false;

    for (u32 i = 0; i < entry->tag_count; i++)
    {
        if (entry->tags[i] && strcmp(entry->tags[i], tag) == 0)
        {
            return true;
        }
    }

    return false;
}

void mel_tile_visual_tile_add_tag(Mel_TileVisual* visual, u32 tile_id, const char* tag)
{
    assert(visual != nullptr);
    assert(tag != nullptr);

    Mel_TileEntry* entry = mel_tile_visual_get_tile(visual, tile_id);
    if (!entry) return;

    if (mel_tile_visual_tile_has_tag(visual, tile_id, tag)) return;

    u32 new_count = entry->tag_count + 1;
    const char** new_tags = (const char**)mel_realloc(visual->alloc, entry->tags, new_count * sizeof(const char*));
    if (!new_tags) return;

    entry->tags = new_tags;
    usize len = strlen(tag);
    entry->tags[entry->tag_count] = (const char*)mel_malloc(visual->alloc, len + 1);
    memcpy((void*)entry->tags[entry->tag_count], tag, len + 1);
    entry->tag_count = new_count;
}

void mel_tile_visual_tile_remove_tag(Mel_TileVisual* visual, u32 tile_id, const char* tag)
{
    assert(visual != nullptr);
    assert(tag != nullptr);

    Mel_TileEntry* entry = mel_tile_visual_get_tile(visual, tile_id);
    if (!entry) return;

    for (u32 i = 0; i < entry->tag_count; i++)
    {
        if (entry->tags[i] && strcmp(entry->tags[i], tag) == 0)
        {
            mel_free(visual->alloc, (void*)entry->tags[i]);

            for (u32 j = i; j < entry->tag_count - 1; j++)
            {
                entry->tags[j] = entry->tags[j + 1];
            }
            entry->tag_count--;
            return;
        }
    }
}
