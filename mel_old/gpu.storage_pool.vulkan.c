#include "gpu.storage_pool.h"
#include "gpu.device.vulkan.h"
#include "gpu.types.vulkan.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

static void mel__storage_pool_ensure_gpu_capacity(Mel_Storage_Pool* pool, Mel_Gpu_Device* dev, u32 needed)
{
    if (needed <= pool->gpu_capacity)
        return;

    u32 new_cap = pool->gpu_capacity;
    while (new_cap < needed)
        new_cap = new_cap == 0 ? 16 : new_cap * 2;

    Mel_Gpu_Buffer old = pool->gpu_buffer;

    mel_gpu_buffer_init(&pool->gpu_buffer, dev,
        .size = (u64)new_cap * pool->item_size,
        .usage = old.usage,
        .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU,
        .map_on_create = true,
    );

    if (old._handle != nullptr)
        mel_gpu_buffer_shutdown(&old, dev);

    pool->gpu_capacity = new_cap;

    mel_bitset_resize(&pool->dirty, new_cap);

    if (pool->has_mirror)
    {
        u8* new_mirror = mel_alloc(pool->alloc, (usize)new_cap * pool->item_size);
        if (pool->mirror)
        {
            memcpy(new_mirror, pool->mirror, (usize)pool->slots.packed_count * pool->item_size);
            mel_dealloc(pool->alloc, pool->mirror);
        }
        pool->mirror = new_mirror;
    }
}

void mel_storage_pool_init_opt(Mel_Storage_Pool* pool, Mel_Storage_Pool_Opt opt)
{
    assert(pool != nullptr);
    assert(opt.dev != nullptr);
    assert(opt.item_size > 0);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    *pool = (Mel_Storage_Pool){0};
    pool->alloc = alloc;
    pool->item_size = opt.item_size;
    pool->has_mirror = opt.cpu_mirror;

    mel_slotmap_init(&pool->slots, alloc, .item_size = opt.item_size, .initial_capacity = opt.initial_capacity);

    u32 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 16;
    mel_bitset_init(&pool->dirty, cap, alloc);

    Mel_Gpu_Buffer_Usage usage = MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_TRANSFER_DST | opt.extra_usage;

    mel_gpu_buffer_init(&pool->gpu_buffer, opt.dev,
        .size = (u64)cap * opt.item_size,
        .usage = usage,
        .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU,
        .map_on_create = true,
    );
    pool->gpu_capacity = cap;

    if (opt.cpu_mirror)
        pool->mirror = mel_alloc(alloc, (usize)cap * opt.item_size);
}

void mel_storage_pool_shutdown(Mel_Storage_Pool* pool, Mel_Gpu_Device* dev)
{
    assert(pool != nullptr);
    assert(dev != nullptr);

    mel_gpu_buffer_shutdown(&pool->gpu_buffer, dev);
    mel_slotmap_free(&pool->slots);
    mel_bitset_free(&pool->dirty);
    if (pool->mirror)
        mel_dealloc(pool->alloc, pool->mirror);

    *pool = (Mel_Storage_Pool){0};
}

Mel_Storage_Handle mel_storage_pool_alloc(Mel_Storage_Pool* pool, const void* data)
{
    assert(pool != nullptr);
    assert(data != nullptr);

    Mel_Storage_Handle handle = mel_slotmap_insert(&pool->slots, data);

    u32 packed_idx = pool->slots.slots[handle.index].packed_idx;

    if (pool->slots.packed_count > pool->dirty.bit_count)
        mel_bitset_resize(&pool->dirty, pool->slots.packed_capacity);

    mel_bitset_set(&pool->dirty, packed_idx);

    if (pool->has_mirror && pool->mirror)
    {
        if (pool->slots.packed_count > pool->gpu_capacity)
        {
            u32 new_cap = pool->gpu_capacity;
            while (new_cap < pool->slots.packed_count)
                new_cap = new_cap == 0 ? 16 : new_cap * 2;

            u8* new_mirror = mel_alloc(pool->alloc, (usize)new_cap * pool->item_size);
            memcpy(new_mirror, pool->mirror, (usize)(pool->slots.packed_count - 1) * pool->item_size);
            mel_dealloc(pool->alloc, pool->mirror);
            pool->mirror = new_mirror;
        }
        memcpy(pool->mirror + packed_idx * pool->item_size, data, pool->item_size);
    }

    return handle;
}

void mel_storage_pool_free(Mel_Storage_Pool* pool, Mel_Storage_Handle handle)
{
    assert(pool != nullptr);
    assert(mel_slotmap_alive(&pool->slots, handle));

    u32 packed_idx = pool->slots.slots[handle.index].packed_idx;
    u32 last_packed = pool->slots.packed_count - 1;

    if (packed_idx != last_packed)
    {
        mel_bitset_clear_bit(&pool->dirty, packed_idx);
        if (mel_bitset_get(&pool->dirty, last_packed))
        {
            mel_bitset_set(&pool->dirty, packed_idx);
            mel_bitset_clear_bit(&pool->dirty, last_packed);
        }
        else
        {
            mel_bitset_set(&pool->dirty, packed_idx);
        }

        if (pool->has_mirror && pool->mirror)
        {
            memcpy(pool->mirror + packed_idx * pool->item_size,
                   pool->mirror + last_packed * pool->item_size,
                   pool->item_size);
        }
    }
    else
    {
        mel_bitset_clear_bit(&pool->dirty, packed_idx);
    }

    mel_slotmap_remove(&pool->slots, handle);
}

void* mel_storage_pool_get(Mel_Storage_Pool* pool, Mel_Storage_Handle handle)
{
    assert(pool != nullptr);
    return mel_slotmap_get(&pool->slots, handle);
}

void mel_storage_pool_set(Mel_Storage_Pool* pool, Mel_Storage_Handle handle, const void* data)
{
    assert(pool != nullptr);
    assert(data != nullptr);

    void* slot = mel_slotmap_get(&pool->slots, handle);
    assert(slot != nullptr);

    memcpy(slot, data, pool->item_size);

    u32 packed_idx = pool->slots.slots[handle.index].packed_idx;
    mel_bitset_set(&pool->dirty, packed_idx);

    if (pool->has_mirror && pool->mirror)
        memcpy(pool->mirror + packed_idx * pool->item_size, data, pool->item_size);
}

void mel_storage_pool_mark_dirty(Mel_Storage_Pool* pool, Mel_Storage_Handle handle)
{
    assert(pool != nullptr);
    assert(mel_slotmap_alive(&pool->slots, handle));

    u32 packed_idx = pool->slots.slots[handle.index].packed_idx;
    mel_bitset_set(&pool->dirty, packed_idx);
}

bool mel_storage_pool_is_dirty(Mel_Storage_Pool* pool)
{
    assert(pool != nullptr);
    return mel_bitset_any(&pool->dirty);
}

void mel_storage_pool_upload_dirty(Mel_Storage_Pool* pool, Mel_Gpu_Device* dev)
{
    assert(pool != nullptr);
    assert(dev != nullptr);

    if (!mel_bitset_any(&pool->dirty))
        return;

    mel__storage_pool_ensure_gpu_capacity(pool, dev, pool->slots.packed_count);

    u8* data = pool->slots.data;

    for (usize i = 0; i < pool->dirty.bit_count && i < pool->slots.packed_count; i++)
    {
        if (!mel_bitset_get(&pool->dirty, i))
            continue;

        mel_gpu_buffer_upload(&pool->gpu_buffer, dev,
            data + i * pool->item_size,
            (u64)pool->item_size,
            (u64)(i * pool->item_size));
    }

    mel_bitset_clear(&pool->dirty);
}

void mel_storage_pool_clear_dirty(Mel_Storage_Pool* pool)
{
    assert(pool != nullptr);
    mel_bitset_clear(&pool->dirty);
}

u32 mel_storage_pool_count(Mel_Storage_Pool* pool)
{
    assert(pool != nullptr);
    return mel_slotmap_count(&pool->slots);
}

Mel_Gpu_Buffer* mel_storage_pool_buffer(Mel_Storage_Pool* pool)
{
    assert(pool != nullptr);
    return &pool->gpu_buffer;
}
