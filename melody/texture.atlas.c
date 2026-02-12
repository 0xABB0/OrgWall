#include "texture.atlas.h"
#include "texture.pool.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "allocator.h"
#include "assets.h"

#include <cjson/cJSON.h>
#include <SDL3/SDL.h>
#include <string.h>

static u64 mel__atlas_pool_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__atlas_pool_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

void mel_atlas_pool_init(Mel_Atlas_Pool* pool, const Mel_Alloc* alloc, Mel_Texture_Pool* tex_pool, Mel_Assets* assets)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(tex_pool != nullptr);
    assert(assets != nullptr);

    *pool = (Mel_Atlas_Pool){0};
    pool->alloc = alloc;
    pool->texture_pool = tex_pool;
    pool->assets = assets;

    mel_slotmap_init(&pool->slotmap, alloc, .item_size = sizeof(Mel_Atlas_Entry), .initial_capacity = 32);
    mel_hashmap_init(&pool->path_to_handle, mel__atlas_pool_hash_key, mel__atlas_pool_eq_key, alloc);
}

void mel_atlas_pool_shutdown(Mel_Atlas_Pool* pool)
{
    assert(pool != nullptr);

    Mel_Atlas_Entry* entries = mel_slotmap_data(&pool->slotmap);
    u32 count = mel_slotmap_count(&pool->slotmap);

    for (u32 i = 0; i < count; i++)
    {
        if (entries[i].regions)
            mel_dealloc(entries[i].alloc, entries[i].regions);
        if (entries[i].region_name_hashes)
            mel_dealloc(entries[i].alloc, entries[i].region_name_hashes);
    }

    mel_slotmap_free(&pool->slotmap);
    mel_hashmap_free(&pool->path_to_handle);

    *pool = (Mel_Atlas_Pool){0};
}

Mel_Atlas_Handle mel_atlas_pool_load(Mel_Atlas_Pool* pool, str8 path)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(path));

    u64 hash = str8_hash(path);

    void* existing = mel_hashmap_get(&pool->path_to_handle, (void*)(usize)hash);
    if (existing)
    {
        Mel_Atlas_Handle h = { .value = (u32)(usize)existing };
        return h;
    }

    char* json_text = mel_assets_read_text(pool->assets, path);
    if (!json_text)
    {
        SDL_Log("texture.atlas: failed to read '%.*s'", (int)path.len, path.data);
        return MEL_ATLAS_HANDLE_NULL;
    }

    cJSON* root = cJSON_Parse(json_text);
    mel_assets_free(pool->assets, json_text);

    if (!root)
    {
        SDL_Log("texture.atlas: failed to parse JSON '%.*s'", (int)path.len, path.data);
        return MEL_ATLAS_HANDLE_NULL;
    }

    cJSON* texture_node = cJSON_GetObjectItem(root, "texture");
    if (!texture_node || !cJSON_IsString(texture_node))
    {
        SDL_Log("texture.atlas: missing 'texture' field in '%.*s'", (int)path.len, path.data);
        cJSON_Delete(root);
        return MEL_ATLAS_HANDLE_NULL;
    }

    Mel_Texture_Handle tex_handle = mel_texture_pool_load(pool->texture_pool, str8_from_cstr(texture_node->valuestring));
    Mel_Gpu_Texture* tex = mel_texture_pool_get(pool->texture_pool, tex_handle);

    cJSON* regions_node = cJSON_GetObjectItem(root, "regions");
    u32 region_count = 0;
    if (regions_node && cJSON_IsArray(regions_node))
        region_count = (u32)cJSON_GetArraySize(regions_node);

    Mel_Atlas_Entry entry = {
        .texture = tex_handle,
        .texture_width = tex->image.width,
        .texture_height = tex->image.height,
        .regions = nullptr,
        .region_count = region_count,
        .region_name_hashes = nullptr,
        .alloc = pool->alloc,
    };

    if (region_count > 0)
    {
        entry.regions = mel_alloc_array(pool->alloc, Mel_Atlas_Region, region_count);
        entry.region_name_hashes = mel_alloc_array(pool->alloc, u64, region_count);

        u32 idx = 0;
        cJSON* r = nullptr;
        cJSON_ArrayForEach(r, regions_node)
        {
            cJSON* name_node = cJSON_GetObjectItem(r, "name");
            cJSON* x_node = cJSON_GetObjectItem(r, "x");
            cJSON* y_node = cJSON_GetObjectItem(r, "y");
            cJSON* w_node = cJSON_GetObjectItem(r, "w");
            cJSON* h_node = cJSON_GetObjectItem(r, "h");
            cJSON* ox_node = cJSON_GetObjectItem(r, "offset_x");
            cJSON* oy_node = cJSON_GetObjectItem(r, "offset_y");

            entry.regions[idx] = (Mel_Atlas_Region){
                .x = x_node ? (u32)x_node->valueint : 0,
                .y = y_node ? (u32)y_node->valueint : 0,
                .width = w_node ? (u32)w_node->valueint : 0,
                .height = h_node ? (u32)h_node->valueint : 0,
                .offset_x = ox_node ? ox_node->valueint : 0,
                .offset_y = oy_node ? oy_node->valueint : 0,
            };

            if (name_node && cJSON_IsString(name_node))
            {
                entry.region_name_hashes[idx] = str8_hash(str8_from_cstr(name_node->valuestring));
            }
            else
            {
                entry.region_name_hashes[idx] = 0;
            }

            idx++;
        }
    }

    cJSON_Delete(root);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    Mel_Atlas_Handle atlas_handle = { .value = sm_handle.value };

    mel_hashmap_put(&pool->path_to_handle, (void*)(usize)hash, (void*)(usize)atlas_handle.value);

    SDL_Log("texture.atlas: loaded '%.*s' (%u regions)", (int)path.len, path.data, region_count);
    return atlas_handle;
}

Mel_Atlas_Entry* mel_atlas_pool_get(Mel_Atlas_Pool* pool, Mel_Atlas_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = { .value = handle.value };
    return mel_slotmap_get(&pool->slotmap, sm_handle);
}

bool mel_atlas_pool_unload(Mel_Atlas_Pool* pool, Mel_Atlas_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = { .value = handle.value };
    Mel_Atlas_Entry* entry = mel_slotmap_get(&pool->slotmap, sm_handle);

    if (!entry)
        return false;

    if (entry->regions)
        mel_dealloc(entry->alloc, entry->regions);
    if (entry->region_name_hashes)
        mel_dealloc(entry->alloc, entry->region_name_hashes);

    mel_hashmap_remove(&pool->path_to_handle, (void*)(usize)entry->texture.value);
    mel_slotmap_remove(&pool->slotmap, sm_handle);

    return true;
}

i32 mel_atlas_find_region(Mel_Atlas_Entry* entry, u64 name_hash)
{
    assert(entry != nullptr);

    for (u32 i = 0; i < entry->region_count; i++)
    {
        if (entry->region_name_hashes[i] == name_hash)
            return (i32)i;
    }

    return -1;
}

void mel_atlas_get_region_uv(Mel_Atlas_Entry* entry, u32 region_idx,
                             f32* u0, f32* v0, f32* u1, f32* v1)
{
    assert(entry != nullptr);
    assert(region_idx < entry->region_count);
    assert(entry->texture_width > 0 && entry->texture_height > 0);

    Mel_Atlas_Region* r = &entry->regions[region_idx];
    f32 inv_w = 1.0f / (f32)entry->texture_width;
    f32 inv_h = 1.0f / (f32)entry->texture_height;

    *u0 = (f32)r->x * inv_w;
    *v0 = (f32)r->y * inv_h;
    *u1 = (f32)(r->x + r->width) * inv_w;
    *v1 = (f32)(r->y + r->height) * inv_h;
}
