#include "texture.pool.h"
#include "texture.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "allocator.h"

#include <SDL3/SDL.h>

static u64 mel__texture_pool_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__texture_pool_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

void mel_texture_pool_init_opt(Mel_Texture_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev, Mel_Texture_Pool_Opt opt)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(dev != nullptr);

    *pool = (Mel_Texture_Pool){0};
    pool->alloc = alloc;
    pool->dev = dev;
    pool->pipeline = opt.pipeline;

    mel_slotmap_init(&pool->slotmap, alloc, .item_size = sizeof(Mel_Texture_Entry), .initial_capacity = 64);
    mel_hashmap_init(&pool->path_to_handle, mel__texture_pool_hash_key, mel__texture_pool_eq_key, alloc);

    pool->job_ctx = opt.job_ctx;
    pool->vfs = opt.vfs;

    mel_gpu_texture_init_white(&pool->fallback, dev);

    if (pool->pipeline)
    {
        pool->fallback.descriptor = mel_gpu_pipeline_alloc_descriptor(pool->pipeline, dev);
        mel_gpu_pipeline_write_texture(pool->pipeline, dev, pool->fallback.descriptor, pool->fallback.image.view, pool->fallback.sampler);
    }
}

void mel_texture_pool_shutdown(Mel_Texture_Pool* pool)
{
    assert(pool != nullptr);

    Mel_Texture_Entry* entries = mel_slotmap_data(&pool->slotmap);
    u32 count = mel_slotmap_count(&pool->slotmap);

    for (u32 i = 0; i < count; i++)
    {
        if (entries[i].state == MEL_TEXTURE_STATE_LOADED)
        {
            mel_gpu_texture_shutdown(&entries[i].gpu_texture, pool->dev);
        }
    }

    mel_gpu_texture_shutdown(&pool->fallback, pool->dev);
    mel_slotmap_free(&pool->slotmap);
    mel_hashmap_free(&pool->path_to_handle);

    *pool = (Mel_Texture_Pool){0};
}

Mel_Texture_Handle mel_texture_pool_load(Mel_Texture_Pool* pool, str8 path)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(path));

    u64 hash = str8_hash(path);

    void* existing = mel_hashmap_get(&pool->path_to_handle, (void*)(usize)hash);
    if (existing)
    {
        Mel_Texture_Handle h = { .handle = mel_slotmap_handle_from_ptr(existing) };
        return h;
    }

    Mel_Texture_Entry entry = {
        .gpu_texture = {0},
        .path_hash = hash,
        .state = MEL_TEXTURE_STATE_UNLOADED,
    };

    if (mel_texture_load(&entry.gpu_texture, pool->dev, pool->vfs, pool->alloc, path))
    {
        if (pool->pipeline)
        {
            entry.gpu_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(pool->pipeline, pool->dev);
            mel_gpu_pipeline_write_texture(pool->pipeline, pool->dev, entry.gpu_texture.descriptor,
                                           entry.gpu_texture.image.view, entry.gpu_texture.sampler);
        }
        entry.state = MEL_TEXTURE_STATE_LOADED;
    }
    else
    {
        entry.state = MEL_TEXTURE_STATE_FAILED;
        SDL_Log("texture.pool: failed to load '%.*s'", (int)path.len, path.data);
    }

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    Mel_Texture_Handle tex_handle = { .handle = sm_handle };

    mel_hashmap_put(&pool->path_to_handle, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));

    return tex_handle;
}

Mel_Gpu_Texture* mel_texture_pool_get(Mel_Texture_Pool* pool, Mel_Texture_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    Mel_Texture_Entry* entry = mel_slotmap_get(&pool->slotmap, sm_handle);

    if (!entry || entry->state != MEL_TEXTURE_STATE_LOADED)
        return &pool->fallback;

    return &entry->gpu_texture;
}

bool mel_texture_pool_unload(Mel_Texture_Pool* pool, Mel_Texture_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    Mel_Texture_Entry* entry = mel_slotmap_get(&pool->slotmap, sm_handle);

    if (!entry)
        return false;

    if (entry->state == MEL_TEXTURE_STATE_LOADED)
        mel_gpu_texture_shutdown(&entry->gpu_texture, pool->dev);

    mel_hashmap_remove(&pool->path_to_handle, (void*)(usize)entry->path_hash);
    mel_slotmap_remove(&pool->slotmap, sm_handle);

    return true;
}

bool mel_texture_pool_is_loaded(Mel_Texture_Pool* pool, Mel_Texture_Handle handle)
{
    assert(pool != nullptr);
    return mel_texture_pool_state(pool, handle) == MEL_TEXTURE_STATE_LOADED;
}

u32 mel_texture_pool_state(Mel_Texture_Pool* pool, Mel_Texture_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    Mel_Texture_Entry* entry = mel_slotmap_get(&pool->slotmap, sm_handle);

    if (!entry)
        return MEL_TEXTURE_STATE_UNLOADED;

    return entry->state;
}

u32 mel_texture_pool_count(Mel_Texture_Pool* pool)
{
    assert(pool != nullptr);
    return mel_slotmap_count(&pool->slotmap);
}

void mel_texture_pool_tick(Mel_Texture_Pool* pool)
{
    assert(pool != nullptr);

    if (!pool->job_ctx)
        return;

    // TODO: check completed async jobs, upload to GPU
    // For now, all loading is synchronous
}
