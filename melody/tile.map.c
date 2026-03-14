#include "tile.map.h"
#include "tile.set.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "allocator.h"
// ASYNC_V2: VFS removed
// #include "vfs.h"
#include <cjson/cJSON.h>
#include <SDL3/SDL.h>
#include <string.h>

static u64 mel__tilemap_pool_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__tilemap_pool_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

static void mel__tilemap_free_entry(Mel_Tilemap_Entry* e)
{
    if (e->name.data)
        mel_dealloc(e->alloc, e->name.data);

    if (e->layers)
    {
        for (u32 i = 0; i < e->layer_count; i++)
        {
            if (e->layers[i].name.data)
                mel_dealloc(e->alloc, e->layers[i].name.data);
            if (e->layers[i].data)
                mel_dealloc(e->alloc, e->layers[i].data);
        }
        mel_dealloc(e->alloc, e->layers);
    }
}

void mel_tilemap_pool_init(Mel_Tilemap_Pool* pool, const Mel_Alloc* alloc, Mel_Tileset_Pool* ts_pool, Mel_Vfs* vfs)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(ts_pool != nullptr);
    assert(vfs != nullptr);

    *pool = (Mel_Tilemap_Pool){0};
    pool->alloc = alloc;
    pool->tileset_pool = ts_pool;
    pool->vfs = vfs;

    mel_slotmap_init(&pool->slotmap, alloc, .item_size = sizeof(Mel_Tilemap_Entry), .initial_capacity = 16);
    mel_hashmap_init(&pool->path_to_handle, mel__tilemap_pool_hash_key, mel__tilemap_pool_eq_key, alloc);
}

void mel_tilemap_pool_shutdown(Mel_Tilemap_Pool* pool)
{
    assert(pool != nullptr);

    Mel_Tilemap_Entry* entries = mel_slotmap_data(&pool->slotmap);
    u32 count = mel_slotmap_count(&pool->slotmap);

    for (u32 i = 0; i < count; i++)
        mel__tilemap_free_entry(&entries[i]);

    mel_slotmap_free(&pool->slotmap);
    mel_hashmap_free(&pool->path_to_handle);
    *pool = (Mel_Tilemap_Pool){0};
}

Mel_Tilemap_Handle mel_tilemap_pool_load(Mel_Tilemap_Pool* pool, str8 path)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(path));

    u64 hash = str8_hash(path);

    void* existing = mel_hashmap_get(&pool->path_to_handle, (void*)(usize)hash);
    if (existing)
    {
        Mel_Tilemap_Handle h = { .handle = mel_slotmap_handle_from_ptr(existing) };
        return h;
    }

    // ASYNC_V2: VFS removed
    SDL_Log("tile.map: VFS removed, cannot load '%.*s'", (int)path.len, path.data);
    return MEL_TILEMAP_HANDLE_NULL;
}

Mel_Tilemap_Handle mel_tilemap_pool_create(Mel_Tilemap_Pool* pool, str8 name, u32 width, u32 height, u32 grid_width, u32 grid_height)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(name));
    assert(width > 0);
    assert(height > 0);
    assert(grid_width > 0);
    assert(grid_height > 0);

    Mel_Tilemap_Entry entry = {0};
    entry.alloc = pool->alloc;
    entry.name = str8_dup(name, pool->alloc);
    entry.width = width;
    entry.height = height;
    entry.grid_width = grid_width;
    entry.grid_height = grid_height;
    entry.tileset = MEL_TILESET_HANDLE_NULL;

    entry.layer_count = 1;
    entry.layers = mel_alloc_array(pool->alloc, Mel_Tilemap_Layer, 1);

    Mel_Tilemap_Layer* l = &entry.layers[0];
    l->name = str8_dup(S8("Layer 1"), pool->alloc);
    l->width = width;
    l->height = height;
    l->visible = true;
    l->parallax_x = 1.0f;
    l->parallax_y = 1.0f;
    l->offset_x = 0;
    l->offset_y = 0;

    u32 tile_count = width * height;
    l->data = (i32*)mel_alloc(pool->alloc, tile_count * sizeof(i32));
    for (u32 i = 0; i < tile_count; i++)
        l->data[i] = -1;

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    Mel_Tilemap_Handle tm_handle = { .handle = sm_handle };

    return tm_handle;
}

Mel_Tilemap_Entry* mel_tilemap_pool_get(Mel_Tilemap_Pool* pool, Mel_Tilemap_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    return mel_slotmap_get(&pool->slotmap, sm_handle);
}

bool mel_tilemap_pool_unload(Mel_Tilemap_Pool* pool, Mel_Tilemap_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    Mel_Tilemap_Entry* entry = mel_slotmap_get(&pool->slotmap, sm_handle);

    if (!entry)
        return false;

    mel__tilemap_free_entry(entry);
    mel_slotmap_remove(&pool->slotmap, sm_handle);
    return true;
}

bool mel_tilemap_pool_save(Mel_Tilemap_Pool* pool, Mel_Tilemap_Handle handle, str8 path)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(path));

    Mel_Tilemap_Entry* entry = mel_tilemap_pool_get(pool, handle);
    if (!entry)
        return false;

    cJSON* root = cJSON_CreateObject();

    if (!str8_is_empty(entry->name))
    {
        char name_buf[256];
        str8_to_buf(entry->name, name_buf, sizeof(name_buf));
        cJSON_AddStringToObject(root, "name", name_buf);
    }

    cJSON_AddNumberToObject(root, "width", entry->width);
    cJSON_AddNumberToObject(root, "height", entry->height);
    cJSON_AddNumberToObject(root, "grid_width", entry->grid_width);
    cJSON_AddNumberToObject(root, "grid_height", entry->grid_height);

    if (entry->layer_count > 0)
    {
        cJSON* layers = cJSON_AddArrayToObject(root, "layers");
        for (u32 i = 0; i < entry->layer_count; i++)
        {
            Mel_Tilemap_Layer* layer = &entry->layers[i];
            cJSON* layer_obj = cJSON_CreateObject();

            if (!str8_is_empty(layer->name))
            {
                char layer_name_buf[256];
                str8_to_buf(layer->name, layer_name_buf, sizeof(layer_name_buf));
                cJSON_AddStringToObject(layer_obj, "name", layer_name_buf);
            }
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
                cJSON_AddItemToArray(data, cJSON_CreateNumber(layer->data[j]));

            cJSON_AddItemToArray(layers, layer_obj);
        }
    }

    char* json_text = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_text)
    {
        SDL_Log("tile.map: failed to serialize JSON");
        return false;
    }

    // ASYNC_V2: VFS removed
    free(json_text);
    SDL_Log("tile.map: VFS removed, cannot save '%.*s'", (int)path.len, path.data);
    return false;
}

i32 mel_tilemap_entry_get_tile(Mel_Tilemap_Entry* entry, u32 layer, u32 x, u32 y)
{
    assert(entry != nullptr);
    assert(layer < entry->layer_count);
    assert(x < entry->width);
    assert(y < entry->height);

    return entry->layers[layer].data[y * entry->width + x];
}

void mel_tilemap_entry_set_tile(Mel_Tilemap_Entry* entry, u32 layer, u32 x, u32 y, i32 tile)
{
    assert(entry != nullptr);
    assert(layer < entry->layer_count);
    assert(x < entry->width);
    assert(y < entry->height);

    entry->layers[layer].data[y * entry->width + x] = tile;
}

Mel_Tilemap_Layer* mel_tilemap_entry_add_layer(Mel_Tilemap_Entry* entry, str8 name)
{
    assert(entry != nullptr);

    u32 new_count = entry->layer_count + 1;
    Mel_Tilemap_Layer* new_layers;
    if (entry->layers)
        new_layers = (Mel_Tilemap_Layer*)mel_realloc(entry->alloc, entry->layers, new_count * sizeof(Mel_Tilemap_Layer));
    else
        new_layers = (Mel_Tilemap_Layer*)mel_alloc(entry->alloc, new_count * sizeof(Mel_Tilemap_Layer));

    if (!new_layers) return nullptr;

    entry->layers = new_layers;
    Mel_Tilemap_Layer* layer = &entry->layers[entry->layer_count];
    entry->layer_count = new_count;

    *layer = (Mel_Tilemap_Layer){0};
    layer->name = str8_dup(name, entry->alloc);
    layer->width = entry->width;
    layer->height = entry->height;
    layer->visible = true;
    layer->parallax_x = 1.0f;
    layer->parallax_y = 1.0f;

    u32 tile_count = entry->width * entry->height;
    layer->data = (i32*)mel_alloc(entry->alloc, tile_count * sizeof(i32));
    for (u32 i = 0; i < tile_count; i++)
        layer->data[i] = -1;

    return layer;
}

bool mel_tilemap_entry_remove_layer(Mel_Tilemap_Entry* entry, u32 layer_idx)
{
    assert(entry != nullptr);

    if (layer_idx >= entry->layer_count) return false;

    Mel_Tilemap_Layer* layer = &entry->layers[layer_idx];
    if (layer->name.data)
        mel_dealloc(entry->alloc, layer->name.data);
    if (layer->data)
        mel_dealloc(entry->alloc, layer->data);

    for (u32 i = layer_idx; i < entry->layer_count - 1; i++)
        entry->layers[i] = entry->layers[i + 1];

    entry->layer_count--;
    return true;
}

bool mel_tilemap_entry_move_layer(Mel_Tilemap_Entry* entry, u32 from_idx, u32 to_idx)
{
    assert(entry != nullptr);

    if (from_idx >= entry->layer_count || to_idx >= entry->layer_count)
        return false;

    if (from_idx == to_idx)
        return true;

    Mel_Tilemap_Layer temp = entry->layers[from_idx];

    if (from_idx < to_idx)
    {
        for (u32 i = from_idx; i < to_idx; i++)
            entry->layers[i] = entry->layers[i + 1];
    }
    else
    {
        for (u32 i = from_idx; i > to_idx; i--)
            entry->layers[i] = entry->layers[i - 1];
    }

    entry->layers[to_idx] = temp;
    return true;
}

bool mel_tilemap_entry_resize(Mel_Tilemap_Entry* entry, u32 new_width, u32 new_height)
{
    assert(entry != nullptr);

    if (new_width == 0 || new_height == 0) return false;
    if (new_width == entry->width && new_height == entry->height) return true;

    for (u32 l = 0; l < entry->layer_count; l++)
    {
        Mel_Tilemap_Layer* layer = &entry->layers[l];

        i32* new_data = (i32*)mel_alloc(entry->alloc, new_width * new_height * sizeof(i32));
        if (!new_data) return false;

        for (u32 i = 0; i < new_width * new_height; i++)
            new_data[i] = -1;

        u32 copy_width = (entry->width < new_width) ? entry->width : new_width;
        u32 copy_height = (entry->height < new_height) ? entry->height : new_height;

        for (u32 y = 0; y < copy_height; y++)
        {
            for (u32 x = 0; x < copy_width; x++)
                new_data[y * new_width + x] = layer->data[y * entry->width + x];
        }

        mel_dealloc(entry->alloc, layer->data);
        layer->data = new_data;
        layer->width = new_width;
        layer->height = new_height;
    }

    entry->width = new_width;
    entry->height = new_height;

    return true;
}
