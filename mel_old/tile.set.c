#include "tile.set.h"
#include "texture.pool.h"
#include "str8.h"
#include "hash.xxh.h"
#include "allocator.h"
#include "vfs.h"
#include "log.h"
#include <cjson/cJSON.h>
#include <string.h>

static u64 mel__tileset_pool_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__tileset_pool_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

static void mel__tileset_entry_free_sources(Mel_Tileset_Entry* entry)
{
    if (!entry->sources) return;

    for (u32 i = 0; i < entry->source_count; i++)
    {
        if (entry->sources[i].path.data)
            mel_dealloc(entry->alloc, entry->sources[i].path.data);
    }
    mel_dealloc(entry->alloc, entry->sources);
    entry->sources = nullptr;
    entry->source_count = 0;
}

static void mel__tileset_entry_free(Mel_Tileset_Entry* entry)
{
    if (entry->name.data)
        mel_dealloc(entry->alloc, entry->name.data);

    mel__tileset_entry_free_sources(entry);

    if (entry->tiles)
        mel_dealloc(entry->alloc, entry->tiles);
}

void mel_tileset_pool_init(Mel_Tileset_Pool* pool, const Mel_Alloc* alloc, Mel_Texture_Pool* tex_pool)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(tex_pool != nullptr);

    *pool = (Mel_Tileset_Pool){0};
    pool->alloc = alloc;
    pool->texture_pool = tex_pool;

    mel_slotmap_init(&pool->slotmap, alloc, .item_size = sizeof(Mel_Tileset_Entry), .initial_capacity = 16);
    mel_hashmap_init(&pool->path_to_handle, mel__tileset_pool_hash_key, mel__tileset_pool_eq_key, alloc);
}

void mel_tileset_pool_shutdown(Mel_Tileset_Pool* pool)
{
    assert(pool != nullptr);

    Mel_Tileset_Entry* entries = mel_slotmap_data(&pool->slotmap);
    u32 count = mel_slotmap_count(&pool->slotmap);

    for (u32 i = 0; i < count; i++)
        mel__tileset_entry_free(&entries[i]);

    mel_slotmap_free(&pool->slotmap);
    mel_hashmap_free(&pool->path_to_handle);
    *pool = (Mel_Tileset_Pool){0};
}

Mel_Tileset_Handle mel_tileset_pool_load(Mel_Tileset_Pool* pool, str8 path)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(path));

    u64 hash = str8_hash(path);

    void* existing = mel_hashmap_get(&pool->path_to_handle, (void*)(usize)hash);
    if (existing)
    {
        Mel_Tileset_Handle h = { .handle = mel_slotmap_handle_from_ptr(existing) };
        return h;
    }

    i64 fsize = 0;
    u8* json_data = mel_vfs_read_file(path, &fsize, pool->alloc);
    if (!json_data)
    {
        mel_log_error("tile.set", "failed to read '%.*s'", (int)path.len, path.data);
        return MEL_TILESET_HANDLE_NULL;
    }

    cJSON* root = cJSON_Parse((const char*)json_data);
    mel_dealloc(pool->alloc, json_data);

    if (!root)
    {
        mel_log_error("tile.set", "failed to parse JSON '%.*s'", (int)path.len, path.data);
        return MEL_TILESET_HANDLE_NULL;
    }

    Mel_Tileset_Entry entry = {0};
    entry.alloc = pool->alloc;

    cJSON* name_json = cJSON_GetObjectItem(root, "name");
    if (name_json && cJSON_IsString(name_json))
        entry.name = str8_dup(str8_from_cstr(name_json->valuestring), pool->alloc);
    else
        entry.name = str8_dup(path, pool->alloc);

    cJSON* sources_json = cJSON_GetObjectItem(root, "sources");
    if (sources_json && cJSON_IsArray(sources_json))
    {
        entry.source_count = (u32)cJSON_GetArraySize(sources_json);
        entry.sources = mel_alloc_array(pool->alloc, Mel_Tile_Source, entry.source_count);

        for (u32 i = 0; i < entry.source_count; i++)
        {
            cJSON* src = cJSON_GetArrayItem(sources_json, (int)i);
            Mel_Tile_Source* s = &entry.sources[i];
            *s = (Mel_Tile_Source){0};

            cJSON* tex_path = cJSON_GetObjectItem(src, "texture");
            if (tex_path && cJSON_IsString(tex_path))
            {
                s->path = str8_dup(str8_from_cstr(tex_path->valuestring), pool->alloc);
                s->texture = mel_texture_pool_load(pool->texture_pool, s->path);
            }

            cJSON* is_sheet = cJSON_GetObjectItem(src, "is_sheet");
            s->is_sheet = is_sheet && cJSON_IsTrue(is_sheet);

            if (s->is_sheet)
            {
                cJSON* tw = cJSON_GetObjectItem(src, "tile_width");
                cJSON* th = cJSON_GetObjectItem(src, "tile_height");
                cJSON* cols = cJSON_GetObjectItem(src, "columns");
                cJSON* rows = cJSON_GetObjectItem(src, "rows");
                cJSON* pad = cJSON_GetObjectItem(src, "padding");
                cJSON* margin = cJSON_GetObjectItem(src, "margin");

                s->tile_width = tw ? (u32)tw->valuedouble : 16;
                s->tile_height = th ? (u32)th->valuedouble : 16;
                s->columns = cols ? (u32)cols->valuedouble : 1;
                s->rows = rows ? (u32)rows->valuedouble : 1;
                s->padding = pad ? (u32)pad->valuedouble : 0;
                s->margin = margin ? (u32)margin->valuedouble : 0;
            }
        }
    }

    cJSON* tiles_json = cJSON_GetObjectItem(root, "tiles");
    if (tiles_json && cJSON_IsArray(tiles_json))
    {
        entry.tile_count = (u32)cJSON_GetArraySize(tiles_json);
        entry.tiles = mel_alloc_array(pool->alloc, Mel_Tile_Def, entry.tile_count);

        for (u32 i = 0; i < entry.tile_count; i++)
        {
            cJSON* tile = cJSON_GetArrayItem(tiles_json, (int)i);
            Mel_Tile_Def* t = &entry.tiles[i];

            t->id = (u32)cJSON_GetObjectItem(tile, "id")->valuedouble;
            t->source_idx = (u32)cJSON_GetObjectItem(tile, "source_idx")->valuedouble;
            t->source_x = (u32)cJSON_GetObjectItem(tile, "source_x")->valuedouble;
            t->source_y = (u32)cJSON_GetObjectItem(tile, "source_y")->valuedouble;
            t->width = (u32)cJSON_GetObjectItem(tile, "width")->valuedouble;
            t->height = (u32)cJSON_GetObjectItem(tile, "height")->valuedouble;

            cJSON* flags_json = cJSON_GetObjectItem(tile, "flags");
            t->flags = flags_json ? (u32)flags_json->valuedouble : 0;
        }
    }

    cJSON_Delete(root);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    mel_hashmap_put(&pool->path_to_handle, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));
    return (Mel_Tileset_Handle){ .handle = sm_handle };
}

Mel_Tileset_Handle mel_tileset_pool_create(Mel_Tileset_Pool* pool, str8 name)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(name));

    Mel_Tileset_Entry entry = {0};
    entry.alloc = pool->alloc;
    entry.name = str8_dup(name, pool->alloc);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    Mel_Tileset_Handle ts_handle = { .handle = sm_handle };

    return ts_handle;
}

Mel_Tileset_Entry* mel_tileset_pool_get(Mel_Tileset_Pool* pool, Mel_Tileset_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    return mel_slotmap_get(&pool->slotmap, sm_handle);
}

bool mel_tileset_pool_unload(Mel_Tileset_Pool* pool, Mel_Tileset_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    Mel_Tileset_Entry* entry = mel_slotmap_get(&pool->slotmap, sm_handle);

    if (!entry)
        return false;

    mel__tileset_entry_free(entry);
    mel_slotmap_remove(&pool->slotmap, sm_handle);
    return true;
}

bool mel_tileset_pool_save(Mel_Tileset_Pool* pool, Mel_Tileset_Handle handle, str8 path)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(path));

    Mel_Tileset_Entry* entry = mel_tileset_pool_get(pool, handle);
    if (!entry)
        return false;

    cJSON* root = cJSON_CreateObject();

    if (!str8_is_empty(entry->name))
    {
        char name_buf[256];
        str8_to_buf(entry->name, name_buf, sizeof(name_buf));
        cJSON_AddStringToObject(root, "name", name_buf);
    }

    if (entry->source_count > 0)
    {
        cJSON* sources = cJSON_AddArrayToObject(root, "sources");
        for (u32 i = 0; i < entry->source_count; i++)
        {
            Mel_Tile_Source* source = &entry->sources[i];
            cJSON* src = cJSON_CreateObject();

            if (!str8_is_empty(source->path))
            {
                char path_buf[512];
                str8_to_buf(source->path, path_buf, sizeof(path_buf));
                cJSON_AddStringToObject(src, "texture", path_buf);
            }
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

    if (entry->tile_count > 0)
    {
        cJSON* tiles = cJSON_AddArrayToObject(root, "tiles");
        for (u32 i = 0; i < entry->tile_count; i++)
        {
            Mel_Tile_Def* t = &entry->tiles[i];
            cJSON* tile = cJSON_CreateObject();

            cJSON_AddNumberToObject(tile, "id", t->id);
            cJSON_AddNumberToObject(tile, "source_idx", t->source_idx);
            cJSON_AddNumberToObject(tile, "source_x", t->source_x);
            cJSON_AddNumberToObject(tile, "source_y", t->source_y);
            cJSON_AddNumberToObject(tile, "width", t->width);
            cJSON_AddNumberToObject(tile, "height", t->height);

            if (t->flags != 0)
                cJSON_AddNumberToObject(tile, "flags", t->flags);

            cJSON_AddItemToArray(tiles, tile);
        }
    }

    char* json_text = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_text)
    {
        mel_log_error("tile.set", "failed to serialize JSON");
        return false;
    }

    i64 json_len = (i64)strlen(json_text);
    bool ok = mel_vfs_write_file(path, json_text, json_len);
    free(json_text);

    if (!ok)
        mel_log_error("tile.set", "failed to write '%.*s'", (int)path.len, path.data);

    return ok;
}

Mel_Tile_Def* mel_tileset_entry_get_tile(Mel_Tileset_Entry* entry, u32 tile_id)
{
    assert(entry != nullptr);

    for (u32 i = 0; i < entry->tile_count; i++)
    {
        if (entry->tiles[i].id == tile_id)
            return &entry->tiles[i];
    }

    return nullptr;
}

Mel_Tile_Source* mel_tileset_entry_add_source(Mel_Tileset_Entry* entry, str8 texture_path, bool is_sheet)
{
    assert(entry != nullptr);
    assert(!str8_is_empty(texture_path));

    u32 new_count = entry->source_count + 1;
    Mel_Tile_Source* new_sources = (Mel_Tile_Source*)mel_realloc(
        entry->alloc, entry->sources, new_count * sizeof(Mel_Tile_Source));

    if (!new_sources) return nullptr;

    entry->sources = new_sources;
    Mel_Tile_Source* source = &entry->sources[entry->source_count];
    entry->source_count = new_count;

    *source = (Mel_Tile_Source){0};
    source->path = str8_dup(texture_path, entry->alloc);
    source->is_sheet = is_sheet;
    source->tile_width = 16;
    source->tile_height = 16;
    source->columns = 1;
    source->rows = 1;

    return source;
}

bool mel_tileset_entry_remove_source(Mel_Tileset_Entry* entry, u32 source_idx)
{
    assert(entry != nullptr);

    if (source_idx >= entry->source_count) return false;

    if (entry->sources[source_idx].path.data)
        mel_dealloc(entry->alloc, entry->sources[source_idx].path.data);

    for (u32 i = source_idx; i < entry->source_count - 1; i++)
        entry->sources[i] = entry->sources[i + 1];

    entry->source_count--;

    u32 new_tile_count = 0;
    for (u32 i = 0; i < entry->tile_count; i++)
    {
        if (entry->tiles[i].source_idx != source_idx)
        {
            if (entry->tiles[i].source_idx > source_idx)
                entry->tiles[i].source_idx--;
            if (new_tile_count != i)
                entry->tiles[new_tile_count] = entry->tiles[i];
            new_tile_count++;
        }
    }
    entry->tile_count = new_tile_count;

    return true;
}

void mel_tileset_entry_regenerate_tiles(Mel_Tileset_Entry* entry, Mel_Texture_Pool* tex_pool)
{
    assert(entry != nullptr);
    assert(tex_pool != nullptr);

    if (entry->tiles)
    {
        mel_dealloc(entry->alloc, entry->tiles);
        entry->tiles = nullptr;
        entry->tile_count = 0;
    }

    u32 total_tiles = 0;
    for (u32 s = 0; s < entry->source_count; s++)
    {
        Mel_Tile_Source* source = &entry->sources[s];
        if (source->is_sheet)
            total_tiles += source->columns * source->rows;
        else
            total_tiles += 1;
    }

    if (total_tiles == 0) return;

    entry->tiles = mel_alloc_array(entry->alloc, Mel_Tile_Def, total_tiles);
    entry->tile_count = total_tiles;

    u32 tile_idx = 0;
    for (u32 s = 0; s < entry->source_count; s++)
    {
        Mel_Tile_Source* source = &entry->sources[s];
        if (source->is_sheet)
        {
            for (u32 y = 0; y < source->rows; y++)
            {
                for (u32 x = 0; x < source->columns; x++)
                {
                    Mel_Tile_Def* t = &entry->tiles[tile_idx];
                    t->id = tile_idx;
                    t->source_idx = s;
                    t->source_x = source->margin + x * (source->tile_width + source->padding);
                    t->source_y = source->margin + y * (source->tile_height + source->padding);
                    t->width = source->tile_width;
                    t->height = source->tile_height;
                    t->flags = 0;
                    tile_idx++;
                }
            }
        }
        else
        {
            Mel_Tile_Def* t = &entry->tiles[tile_idx];
            t->id = tile_idx;
            t->source_idx = s;
            t->source_x = 0;
            t->source_y = 0;
            t->flags = 0;

            Mel_Gpu_Texture* gpu_tex = mel_texture_pool_get(tex_pool, source->texture);
            if (gpu_tex)
            {
                t->width = gpu_tex->image.width;
                t->height = gpu_tex->image.height;
            }
            else
            {
                t->width = 0;
                t->height = 0;
            }
            tile_idx++;
        }
    }
}
