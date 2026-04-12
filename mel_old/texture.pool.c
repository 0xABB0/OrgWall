#include "texture.pool.h"
#include "texture.h"
#include "render.texture_table.h"
#include "core.engine.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "gpu.texture.h"
#include "gpu.device.h"
#include "gpu.pipeline.h"
#include "collection.hashmap.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "log.h"

#include <SDL3/SDL.h>

static Mel_Texture_Pool s_texture_pool;
static Mel_Texture_Table s_texture_table;
static Mel_HashMap s_table_dedup;

Mel_Texture_Pool* mel_texture_pool(void)
{
    return &s_texture_pool;
}

Mel_Texture_Table* mel_texture_pool_get_table(void)
{
    return &s_texture_table;
}

u32 mel_texture_pool_add_to_table(Mel_Gpu_Texture* tex)
{
    assert(tex != nullptr);
    assert(s_texture_table.capacity > 0);

    void* key = tex->image._view;
    void* existing = mel_hashmap_get(&s_table_dedup, key);
    if (existing)
        return (u32)(usize)existing - 1;

    u32 idx = mel_texture_table_add(&s_texture_table, tex->image._view, tex->_sampler);
    mel_hashmap_put(&s_table_dedup, key, (void*)(usize)(idx + 1));
    return idx;
}

Mel_Event_Channel mel_texture_pool_ready;

static void mel__texture_pool_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    Mel_Gpu_Device* dev = e->dev;
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_texture_pool_init(&s_texture_pool, alloc, dev);

    mel_texture_table_init(&s_texture_table, dev, alloc, .capacity = 1024);
    mel_hashmap_init(&s_table_dedup, mel_hashmap_hash_ptr, mel_hashmap_eq_u64, alloc);

    s_texture_pool.table = &s_texture_table;
    s_texture_pool.white_table_idx = mel_texture_table_add(&s_texture_table,
        s_texture_pool.fallback.image._view, s_texture_pool.fallback._sampler);

    mel_event_channel_fire(&mel_texture_pool_ready, NULL);
    mel_log_info("texture.pool", "initialized with bindless table (cap=%u)", 1024);
}

static void mel__texture_pool_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
    mel_hashmap_free(&s_table_dedup);
    if (s_texture_table.capacity > 0)
        mel_texture_table_shutdown(&s_texture_table);
    if (s_texture_pool.dev)
        mel_texture_pool_shutdown(&s_texture_pool);
}

static void mel__texture_pool_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__texture_pool_on_gpu_ready, NULL);
    mel_event_channel_on(&mel_shutdown_begin, mel__texture_pool_on_shutdown, NULL);
}

__attribute__((constructor))
static void mel__texture_pool_register(void)
{
    mel_event_channel_init(&mel_texture_pool_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__texture_pool_wire);
}

__attribute__((destructor))
static void mel__texture_pool_unregister(void)
{
    mel_event_channel_destroy(&mel_texture_pool_ready);
}

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

    mel_gpu_texture_init_white(&pool->fallback, dev);

    if (pool->pipeline)
    {
        pool->fallback._descriptor = mel_gpu_pipeline_alloc_descriptor(pool->pipeline, dev);
        mel_gpu_pipeline_write_texture(pool->pipeline, dev, pool->fallback._descriptor, pool->fallback.image._view, pool->fallback._sampler);
    }
}

void mel_texture_pool_shutdown(Mel_Texture_Pool* pool)
{
    assert(pool != nullptr);

    Mel_Texture_Entry* entries = mel_slotmap_data(&pool->slotmap);
    u32 count = mel_slotmap_count(&pool->slotmap);

    for (u32 i = 0; i < count; i++)
    {
        if (entries[i].state == MEL_TEXTURE_STATE_LOADED && !entries[i].external)
        {
            mel_gpu_texture_shutdown(&entries[i].gpu_texture, pool->dev);
        }
    }

    mel_gpu_texture_shutdown(&pool->fallback, pool->dev);
    mel_slotmap_free(&pool->slotmap);
    mel_hashmap_free(&pool->path_to_handle);

    *pool = (Mel_Texture_Pool){0};
}

Mel_Texture_Handle mel_texture_pool_load_opt(Mel_Texture_Pool* pool, str8 path, Mel_Texture_Pool_Load_Opt opt)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(path));

    struct {
        u64 path_hash;
        u32 format;
        u8 nearest_filter;
        u8 generate_mips;
        u8 address_mode_u;
        u8 address_mode_v;
        u8 address_mode_w;
    } key_desc = {
        .path_hash = str8_hash(path),
        .format = opt.format ? opt.format : MEL_GPU_FORMAT_R8G8B8A8_SRGB,
        .nearest_filter = opt.nearest_filter ? 1u : 0u,
        .generate_mips = opt.generate_mips ? 1u : 0u,
        .address_mode_u = (u8)opt.address_mode_u,
        .address_mode_v = (u8)opt.address_mode_v,
        .address_mode_w = (u8)opt.address_mode_w,
    };
    u64 hash = mel_xxh64(&key_desc, sizeof(key_desc), 0);

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

    if (mel_texture_load(&entry.gpu_texture, pool->dev, pool->alloc, path,
            .format = key_desc.format,
            .nearest_filter = key_desc.nearest_filter != 0,
            .generate_mips = key_desc.generate_mips != 0,
            .address_mode_u = key_desc.address_mode_u,
            .address_mode_v = key_desc.address_mode_v,
            .address_mode_w = key_desc.address_mode_w))
    {
        if (pool->pipeline)
        {
            entry.gpu_texture._descriptor = mel_gpu_pipeline_alloc_descriptor(pool->pipeline, pool->dev);
            mel_gpu_pipeline_write_texture(pool->pipeline, pool->dev, entry.gpu_texture._descriptor,
                                           entry.gpu_texture.image._view, entry.gpu_texture._sampler);
        }
        entry.state = MEL_TEXTURE_STATE_LOADED;
    }
    else
    {
        entry.state = MEL_TEXTURE_STATE_FAILED;
        mel_log_error("texture.pool", "failed to load '%.*s'", (int)path.len, path.data);
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

    (void)pool;
}

Mel_Texture_Handle mel_texture_pool_register(Mel_Texture_Pool* pool, Mel_Gpu_Texture* tex)
{
    assert(pool != nullptr);
    assert(tex != nullptr);

    Mel_Texture_Entry entry = {
        .gpu_texture = *tex,
        .path_hash = 0,
        .state = MEL_TEXTURE_STATE_LOADED,
        .external = true,
    };

    if (pool->pipeline)
    {
        entry.gpu_texture._descriptor = mel_gpu_pipeline_alloc_descriptor(pool->pipeline, pool->dev);
        mel_gpu_pipeline_write_texture(pool->pipeline, pool->dev, entry.gpu_texture._descriptor,
                                       entry.gpu_texture.image._view, entry.gpu_texture._sampler);
    }

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    return (Mel_Texture_Handle){ .handle = sm_handle };
}
