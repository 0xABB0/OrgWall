#include "texture.atlas.h"
#include "texture.pool.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "allocator.h"
#include "vfs.h"
#include "log.h"

#include <cjson/cJSON.h>
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

void mel_atlas_pool_init(Mel_Atlas_Pool* pool, const Mel_Alloc* alloc, Mel_Texture_Pool* tex_pool)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(tex_pool != nullptr);

    *pool = (Mel_Atlas_Pool){0};
    pool->alloc = alloc;
    pool->texture_pool = tex_pool;

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
        Mel_Atlas_Handle h = { .handle = mel_slotmap_handle_from_ptr(existing) };
        return h;
    }

    i64 fsize = 0;
    u8* json_data = mel_vfs_read_file(path, &fsize, pool->alloc);
    if (!json_data)
    {
        mel_log_error("texture.atlas", "failed to read '%.*s'", (int)path.len, path.data);
        return MEL_ATLAS_HANDLE_NULL;
    }

    cJSON* root = cJSON_Parse((const char*)json_data);
    mel_dealloc(pool->alloc, json_data);

    if (!root)
    {
        mel_log_error("texture.atlas", "failed to parse JSON '%.*s'", (int)path.len, path.data);
        return MEL_ATLAS_HANDLE_NULL;
    }

    Mel_Atlas_Entry entry = {0};
    entry.alloc = pool->alloc;

    cJSON* tex_path = cJSON_GetObjectItem(root, "texture");
    if (tex_path && cJSON_IsString(tex_path))
        entry.texture = mel_texture_pool_load(pool->texture_pool, str8_from_cstr(tex_path->valuestring));

    cJSON* tw = cJSON_GetObjectItem(root, "texture_width");
    cJSON* th = cJSON_GetObjectItem(root, "texture_height");
    entry.texture_width = tw ? (u32)tw->valuedouble : 0;
    entry.texture_height = th ? (u32)th->valuedouble : 0;

    cJSON* regions_json = cJSON_GetObjectItem(root, "regions");
    if (regions_json && cJSON_IsArray(regions_json))
    {
        entry.region_count = (u32)cJSON_GetArraySize(regions_json);
        entry.regions = mel_alloc_array(pool->alloc, Mel_Atlas_Region, entry.region_count);
        entry.region_name_hashes = mel_alloc_array(pool->alloc, u64, entry.region_count);

        for (u32 i = 0; i < entry.region_count; i++)
        {
            cJSON* reg = cJSON_GetArrayItem(regions_json, (int)i);
            Mel_Atlas_Region* r = &entry.regions[i];

            r->x = (u32)cJSON_GetObjectItem(reg, "x")->valuedouble;
            r->y = (u32)cJSON_GetObjectItem(reg, "y")->valuedouble;
            r->width = (u32)cJSON_GetObjectItem(reg, "width")->valuedouble;
            r->height = (u32)cJSON_GetObjectItem(reg, "height")->valuedouble;

            cJSON* ox = cJSON_GetObjectItem(reg, "offset_x");
            cJSON* oy = cJSON_GetObjectItem(reg, "offset_y");
            r->offset_x = ox ? (i32)ox->valuedouble : 0;
            r->offset_y = oy ? (i32)oy->valuedouble : 0;

            cJSON* name = cJSON_GetObjectItem(reg, "name");
            if (name && cJSON_IsString(name))
                entry.region_name_hashes[i] = str8_hash(str8_from_cstr(name->valuestring));
            else
                entry.region_name_hashes[i] = 0;
        }
    }

    cJSON_Delete(root);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    mel_hashmap_put(&pool->path_to_handle, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));
    return (Mel_Atlas_Handle){ .handle = sm_handle };
}

Mel_Atlas_Entry* mel_atlas_pool_get(Mel_Atlas_Pool* pool, Mel_Atlas_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    return mel_slotmap_get(&pool->slotmap, sm_handle);
}

bool mel_atlas_pool_unload(Mel_Atlas_Pool* pool, Mel_Atlas_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    Mel_Atlas_Entry* entry = mel_slotmap_get(&pool->slotmap, sm_handle);

    if (!entry)
        return false;

    if (entry->regions)
        mel_dealloc(entry->alloc, entry->regions);
    if (entry->region_name_hashes)
        mel_dealloc(entry->alloc, entry->region_name_hashes);

    mel_hashmap_remove(&pool->path_to_handle, mel_slotmap_handle_to_ptr(entry->texture.handle));
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
