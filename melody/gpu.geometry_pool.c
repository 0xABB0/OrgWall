#include "gpu.geometry_pool.h"
#include "gpu.device.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

static bool mel__geometry_is_unified(Mel_Gpu_Device* dev)
{
    VkPhysicalDeviceMemoryProperties mem = {0};
    vkGetPhysicalDeviceMemoryProperties(dev->physical_device, &mem);

    for (u32 i = 0; i < mem.memoryHeapCount; i++)
    {
        if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            bool all_types_host_visible = true;
            for (u32 j = 0; j < mem.memoryTypeCount; j++)
            {
                if (mem.memoryTypes[j].heapIndex != i)
                    continue;
                if (!(mem.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                    continue;
                if (!(mem.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                {
                    all_types_host_visible = false;
                    break;
                }
            }
            if (!all_types_host_visible)
                return false;
        }
    }
    return true;
}

static void mel__lane_init(Mel_Geometry_Lane* lane, Mel_Geometry_Pool* pool,
                            u32 stride, u64 capacity,
                            Mel_Gpu_Buffer_Usage gpu_usage)
{
    *lane = (Mel_Geometry_Lane){0};
    lane->stride = stride;
    lane->capacity = capacity;
    mel_array_init(&lane->free_list, pool->alloc);

    if (capacity == 0)
        return;

    if (pool->storage_mode == MEL_GEOMETRY_STORAGE_CPU)
    {
        lane->cpu = mel_alloc(pool->alloc, capacity);
        memset(lane->cpu, 0, capacity);
    }
    else
    {
        Mel_Gpu_Memory_Usage mem_usage = (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
            ? MEL_GPU_MEMORY_USAGE_CPU_TO_GPU
            : MEL_GPU_MEMORY_USAGE_GPU_ONLY;

        mel_gpu_buffer_init(&lane->gpu, pool->dev,
            .size = capacity,
            .usage = gpu_usage | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = mem_usage,
            .map_on_create = (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED));

        if (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
            lane->cpu = lane->gpu.mapped;
    }
}

static void mel__lane_init_lazy(Mel_Geometry_Lane* lane, const Mel_Alloc* alloc)
{
    *lane = (Mel_Geometry_Lane){0};
    mel_array_init(&lane->free_list, alloc);
}

static void mel__lane_ensure_capacity(Mel_Geometry_Lane* lane, Mel_Geometry_Pool* pool,
                                       u64 needed, Mel_Gpu_Buffer_Usage gpu_usage)
{
    if (needed <= lane->capacity)
        return;

    u64 new_cap = lane->capacity;
    if (new_cap == 0) new_cap = 4096;
    while (new_cap < needed)
        new_cap *= 2;

    if (pool->storage_mode == MEL_GEOMETRY_STORAGE_CPU)
    {
        u8* new_buf = mel_alloc(pool->alloc, new_cap);
        if (lane->cpu)
        {
            memcpy(new_buf, lane->cpu, lane->cursor);
            mel_dealloc(pool->alloc, lane->cpu);
        }
        lane->cpu = new_buf;
    }
    else
    {
        Mel_Gpu_Memory_Usage mem_usage = (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
            ? MEL_GPU_MEMORY_USAGE_CPU_TO_GPU
            : MEL_GPU_MEMORY_USAGE_GPU_ONLY;

        Mel_Gpu_Buffer old = lane->gpu;

        mel_gpu_buffer_init(&lane->gpu, pool->dev,
            .size = new_cap,
            .usage = gpu_usage | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = mem_usage,
            .map_on_create = (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED));

        if (old._handle != nullptr && lane->cursor > 0)
        {
            if (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
            {
                memcpy(lane->gpu.mapped, old.mapped, lane->cursor);
            }
            else
            {
                void* src = mel_gpu_buffer_map(&old, pool->dev);
                mel_gpu_buffer_upload(&lane->gpu, pool->dev, src, lane->cursor, 0);
                mel_gpu_buffer_unmap(&old, pool->dev);
            }
        }

        if (old._handle != nullptr)
            mel_gpu_buffer_shutdown(&old, pool->dev);

        if (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
            lane->cpu = lane->gpu.mapped;
        else if (lane->cpu != nullptr)
        {
            u8* new_mirror = mel_alloc(pool->alloc, new_cap);
            memcpy(new_mirror, lane->cpu, lane->cursor);
            mel_dealloc(pool->alloc, lane->cpu);
            lane->cpu = new_mirror;
        }
    }

    lane->capacity = new_cap;
}

static void mel__lane_shutdown(Mel_Geometry_Lane* lane, Mel_Geometry_Pool* pool)
{
    if (lane->gpu._handle != nullptr)
        mel_gpu_buffer_shutdown(&lane->gpu, pool->dev);

    if (pool->storage_mode != MEL_GEOMETRY_STORAGE_UNIFIED && lane->cpu != nullptr)
        mel_dealloc(pool->alloc, lane->cpu);

    mel_array_free(&lane->free_list);
    *lane = (Mel_Geometry_Lane){0};
}

static void mel__free_list_merge(Mel_Geometry_Free_List* list)
{
    if (list->count < 2)
        return;

    for (usize i = 0; i < list->count; i++)
    {
        for (usize j = i + 1; j < list->count; j++)
        {
            Mel_Geometry_Free_Region* a = &list->items[i];
            Mel_Geometry_Free_Region* b = &list->items[j];

            if (a->offset + a->size == b->offset)
            {
                a->size += b->size;
                list->items[j] = list->items[list->count - 1];
                list->count--;
                j--;
            }
            else if (b->offset + b->size == a->offset)
            {
                a->offset = b->offset;
                a->size += b->size;
                list->items[j] = list->items[list->count - 1];
                list->count--;
                j--;
            }
        }
    }
}

static u64 mel__lane_alloc(Mel_Geometry_Lane* lane, Mel_Geometry_Pool* pool,
                            u64 size, Mel_Gpu_Buffer_Usage gpu_usage)
{
    for (usize i = 0; i < lane->free_list.count; i++)
    {
        Mel_Geometry_Free_Region* fr = &lane->free_list.items[i];
        if (fr->size >= size)
        {
            u64 offset = fr->offset;
            if (fr->size == size)
            {
                lane->free_list.items[i] = lane->free_list.items[lane->free_list.count - 1];
                lane->free_list.count--;
            }
            else
            {
                fr->offset += size;
                fr->size -= size;
            }
            return offset;
        }
    }

    mel__lane_ensure_capacity(lane, pool, lane->cursor + size, gpu_usage);
    u64 offset = lane->cursor;
    lane->cursor += size;
    return offset;
}

static void mel__lane_free(Mel_Geometry_Lane* lane, u64 offset, u64 size)
{
    mel_array_push(&lane->free_list, ((Mel_Geometry_Free_Region){ .offset = offset, .size = size }));
    mel__free_list_merge(&lane->free_list);
}

static void mel__lane_write(Mel_Geometry_Lane* lane, Mel_Geometry_Pool* pool,
                             u64 offset, const void* data, u64 size)
{
    assert(offset + size <= lane->capacity);

    if (pool->storage_mode == MEL_GEOMETRY_STORAGE_CPU)
    {
        memcpy(lane->cpu + offset, data, size);
    }
    else if (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
    {
        memcpy(lane->cpu + offset, data, size);
    }
    else
    {
        mel_gpu_buffer_upload(&lane->gpu, pool->dev, data, size, offset);
        if (lane->cpu != nullptr)
            memcpy(lane->cpu + offset, data, size);
    }
}

void mel_geometry_pool_init_opt(Mel_Geometry_Pool* pool, Mel_Geometry_Pool_Opt opt)
{
    assert(pool != nullptr);
    assert(opt.vertex_stride > 0);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    *pool = (Mel_Geometry_Pool){0};
    pool->dev = opt.dev;
    pool->alloc = alloc;

    if (opt.dev == nullptr)
    {
        pool->storage_mode = MEL_GEOMETRY_STORAGE_CPU;
    }
    else if (mel__geometry_is_unified(opt.dev))
    {
        pool->storage_mode = MEL_GEOMETRY_STORAGE_UNIFIED;
    }
    else
    {
        pool->storage_mode = MEL_GEOMETRY_STORAGE_DISCRETE;
    }

    mel_slotmap_init(&pool->catalog, alloc,
        .item_size = sizeof(Mel_Geometry_Region),
        .initial_capacity = 32);

    if (pool->storage_mode != MEL_GEOMETRY_STORAGE_CPU)
    {
        u32 catalog_cap = 32;
        mel_gpu_buffer_init(&pool->catalog_gpu, opt.dev,
            .size = (u64)catalog_cap * sizeof(Mel_Geometry_Region),
            .usage = MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
                ? MEL_GPU_MEMORY_USAGE_CPU_TO_GPU
                : MEL_GPU_MEMORY_USAGE_GPU_ONLY);
    }

    mel_bitset_init(&pool->catalog_dirty, 32, alloc);

    u64 vert_cap = opt.vertex_capacity > 0 ? opt.vertex_capacity : (u64)opt.vertex_stride * 1024;
    mel__lane_init(&pool->vertices, pool, opt.vertex_stride, vert_cap,
                    MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_VERTEX);

    if (opt.cpu_readable && pool->storage_mode == MEL_GEOMETRY_STORAGE_DISCRETE)
    {
        pool->vertices.cpu = mel_alloc(alloc, vert_cap);
        memset(pool->vertices.cpu, 0, vert_cap);
    }

    mel__lane_init_lazy(&pool->indices_u16, alloc);
    pool->indices_u16.stride = sizeof(u16);
    mel__lane_init_lazy(&pool->indices_u32, alloc);
    pool->indices_u32.stride = sizeof(u32);
    mel__lane_init_lazy(&pool->meshlet_descs, alloc);
    mel__lane_init_lazy(&pool->meshlet_data, alloc);
}

void mel_geometry_pool_shutdown(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);

    mel__lane_shutdown(&pool->meshlet_data, pool);
    mel__lane_shutdown(&pool->meshlet_descs, pool);
    mel__lane_shutdown(&pool->indices_u32, pool);
    mel__lane_shutdown(&pool->indices_u16, pool);
    mel__lane_shutdown(&pool->vertices, pool);

    if (pool->catalog_gpu._handle != nullptr)
        mel_gpu_buffer_shutdown(&pool->catalog_gpu, pool->dev);

    mel_bitset_free(&pool->catalog_dirty);
    mel_slotmap_free(&pool->catalog);

    *pool = (Mel_Geometry_Pool){0};
}

Mel_Geometry_Handle mel_geometry_pool_upload(Mel_Geometry_Pool* pool,
                                             const Mel_Geometry_Upload* upload)
{
    assert(pool != nullptr);
    assert(upload != nullptr);
    assert(upload->vertices != nullptr);
    assert(upload->vertex_count > 0);

    u64 vertex_bytes = (u64)upload->vertex_count * pool->vertices.stride;
    u64 vertex_offset = mel__lane_alloc(&pool->vertices, pool, vertex_bytes,
        MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_VERTEX);
    mel__lane_write(&pool->vertices, pool, vertex_offset, upload->vertices, vertex_bytes);

    u32 index_offset = 0;
    u32 index_type = upload->index_type;

    if (upload->indices != nullptr && upload->index_count > 0)
    {
        u32 idx_stride = (index_type == MEL_GPU_INDEX_TYPE_U16) ? sizeof(u16) : sizeof(u32);
        u64 index_bytes = (u64)upload->index_count * idx_stride;
        Mel_Geometry_Lane* idx_lane = (index_type == MEL_GPU_INDEX_TYPE_U16)
            ? &pool->indices_u16
            : &pool->indices_u32;

        Mel_Gpu_Buffer_Usage idx_usage = MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_INDEX;
        u64 byte_offset = mel__lane_alloc(idx_lane, pool, index_bytes, idx_usage);
        mel__lane_write(idx_lane, pool, byte_offset, upload->indices, index_bytes);

        index_offset = (u32)(byte_offset / idx_stride);
    }

    u32 meshlet_offset = 0;
    u32 meshlet_count = 0;

    if (upload->meshlet_descs != nullptr && upload->meshlet_count > 0)
    {
        u64 desc_bytes = (u64)upload->meshlet_count * pool->meshlet_descs.stride;
        u64 desc_off = mel__lane_alloc(&pool->meshlet_descs, pool, desc_bytes,
            MEL_GPU_BUFFER_USAGE_STORAGE);

        mel__lane_write(&pool->meshlet_descs, pool, desc_off,
                         upload->meshlet_descs, desc_bytes);

        meshlet_offset = (u32)(desc_off / pool->meshlet_descs.stride);
        meshlet_count = upload->meshlet_count;

        if (upload->meshlet_data != nullptr && upload->meshlet_data_size > 0)
        {
            u64 data_off = mel__lane_alloc(&pool->meshlet_data, pool,
                upload->meshlet_data_size, MEL_GPU_BUFFER_USAGE_STORAGE);
            mel__lane_write(&pool->meshlet_data, pool, data_off,
                             upload->meshlet_data, upload->meshlet_data_size);
        }
    }

    Mel_Geometry_Region region = {
        .vertex_offset = (u32)(vertex_offset / pool->vertices.stride),
        .vertex_count = upload->vertex_count,
        .index_offset = index_offset,
        .index_count = upload->index_count,
        .meshlet_offset = meshlet_offset,
        .meshlet_count = meshlet_count,
        .index_type = index_type,
        ._pad = 0,
    };

    Mel_SlotMap_Handle sh = mel_slotmap_insert(&pool->catalog, &region);

    u32 packed_idx = pool->catalog.slots[sh.index].packed_idx;
    if (packed_idx >= pool->catalog_dirty.bit_count)
        mel_bitset_resize(&pool->catalog_dirty, pool->catalog.packed_capacity);
    mel_bitset_set(&pool->catalog_dirty, packed_idx);

    return (Mel_Geometry_Handle){ .handle = sh };
}

void mel_geometry_pool_remove(Mel_Geometry_Pool* pool, Mel_Geometry_Handle h)
{
    assert(pool != nullptr);
    assert(mel_slotmap_alive(&pool->catalog, h.handle));

    Mel_Geometry_Region* region = mel_slotmap_get(&pool->catalog, h.handle);
    assert(region != nullptr);

    u64 vertex_bytes = (u64)region->vertex_count * pool->vertices.stride;
    u64 vertex_byte_offset = (u64)region->vertex_offset * pool->vertices.stride;
    mel__lane_free(&pool->vertices, vertex_byte_offset, vertex_bytes);

    if (region->index_count > 0)
    {
        Mel_Geometry_Lane* idx_lane = (region->index_type == MEL_GPU_INDEX_TYPE_U16)
            ? &pool->indices_u16
            : &pool->indices_u32;
        u32 idx_stride = (region->index_type == MEL_GPU_INDEX_TYPE_U16) ? sizeof(u16) : sizeof(u32);
        u64 index_bytes = (u64)region->index_count * idx_stride;
        u64 index_byte_offset = (u64)region->index_offset * idx_stride;
        mel__lane_free(idx_lane, index_byte_offset, index_bytes);
    }

    u32 packed_idx = pool->catalog.slots[h.handle.index].packed_idx;
    u32 last_packed = pool->catalog.packed_count - 1;

    if (packed_idx != last_packed)
    {
        if (packed_idx < pool->catalog_dirty.bit_count)
            mel_bitset_set(&pool->catalog_dirty, packed_idx);
        if (last_packed < pool->catalog_dirty.bit_count)
        {
            if (mel_bitset_get(&pool->catalog_dirty, last_packed))
                mel_bitset_clear_bit(&pool->catalog_dirty, last_packed);
            mel_bitset_set(&pool->catalog_dirty, packed_idx);
        }
    }

    mel_slotmap_remove(&pool->catalog, h.handle);
}

Mel_Geometry_Region mel_geometry_pool_region(Mel_Geometry_Pool* pool, Mel_Geometry_Handle h)
{
    assert(pool != nullptr);
    Mel_Geometry_Region* r = mel_slotmap_get(&pool->catalog, h.handle);
    assert(r != nullptr);
    return *r;
}

static void mel__catalog_ensure_gpu_capacity(Mel_Geometry_Pool* pool, u32 needed)
{
    if (pool->storage_mode == MEL_GEOMETRY_STORAGE_CPU)
        return;

    u64 current_cap = pool->catalog_gpu.size / sizeof(Mel_Geometry_Region);
    if (needed <= current_cap)
        return;

    u64 new_cap = current_cap;
    while (new_cap < needed)
        new_cap = new_cap == 0 ? 32 : new_cap * 2;

    Mel_Gpu_Buffer old = pool->catalog_gpu;

    mel_gpu_buffer_init(&pool->catalog_gpu, pool->dev,
        .size = new_cap * sizeof(Mel_Geometry_Region),
        .usage = MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
        .memory_usage = (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
            ? MEL_GPU_MEMORY_USAGE_CPU_TO_GPU
            : MEL_GPU_MEMORY_USAGE_GPU_ONLY);

    if (old._handle != nullptr)
        mel_gpu_buffer_shutdown(&old, pool->dev);
}

void mel_geometry_pool_upload_catalog(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);

    if (pool->storage_mode == MEL_GEOMETRY_STORAGE_CPU)
        return;

    if (!mel_bitset_any(&pool->catalog_dirty))
        return;

    mel__catalog_ensure_gpu_capacity(pool, pool->catalog.packed_count);

    Mel_Geometry_Region* data = (Mel_Geometry_Region*)pool->catalog.data;

    if (pool->storage_mode == MEL_GEOMETRY_STORAGE_UNIFIED)
    {
        u8* dst = pool->catalog_gpu.mapped;
        assert(dst != nullptr);
        for (usize i = 0; i < pool->catalog_dirty.bit_count && i < pool->catalog.packed_count; i++)
        {
            if (!mel_bitset_get(&pool->catalog_dirty, i))
                continue;
            memcpy(dst + i * sizeof(Mel_Geometry_Region),
                   &data[i], sizeof(Mel_Geometry_Region));
        }
    }
    else
    {
        for (usize i = 0; i < pool->catalog_dirty.bit_count && i < pool->catalog.packed_count; i++)
        {
            if (!mel_bitset_get(&pool->catalog_dirty, i))
                continue;
            mel_gpu_buffer_upload(&pool->catalog_gpu, pool->dev,
                &data[i], sizeof(Mel_Geometry_Region),
                (u64)(i * sizeof(Mel_Geometry_Region)));
        }
    }

    mel_bitset_clear(&pool->catalog_dirty);
}

Mel_Gpu_Buffer* mel_geometry_pool_catalog_buffer(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    assert(pool->storage_mode != MEL_GEOMETRY_STORAGE_CPU);
    return &pool->catalog_gpu;
}

Mel_Gpu_Buffer* mel_geometry_pool_vertex_buffer(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    assert(pool->storage_mode != MEL_GEOMETRY_STORAGE_CPU);
    return &pool->vertices.gpu;
}

Mel_Gpu_Buffer* mel_geometry_pool_index_buffer_u16(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    assert(pool->storage_mode != MEL_GEOMETRY_STORAGE_CPU);
    if (pool->indices_u16.gpu._handle == nullptr)
        return nullptr;
    return &pool->indices_u16.gpu;
}

Mel_Gpu_Buffer* mel_geometry_pool_index_buffer_u32(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    assert(pool->storage_mode != MEL_GEOMETRY_STORAGE_CPU);
    if (pool->indices_u32.gpu._handle == nullptr)
        return nullptr;
    return &pool->indices_u32.gpu;
}

Mel_Gpu_Buffer* mel_geometry_pool_meshlet_desc_buffer(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    assert(pool->storage_mode != MEL_GEOMETRY_STORAGE_CPU);
    if (pool->meshlet_descs.gpu._handle == nullptr)
        return nullptr;
    return &pool->meshlet_descs.gpu;
}

Mel_Gpu_Buffer* mel_geometry_pool_meshlet_data_buffer(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    assert(pool->storage_mode != MEL_GEOMETRY_STORAGE_CPU);
    if (pool->meshlet_data.gpu._handle == nullptr)
        return nullptr;
    return &pool->meshlet_data.gpu;
}

u32 mel_geometry_pool_vertex_stride(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    return pool->vertices.stride;
}

const void* mel_geometry_pool_cpu_vertices(Mel_Geometry_Pool* pool, Mel_Geometry_Handle h)
{
    assert(pool != nullptr);
    assert(pool->vertices.cpu != nullptr);

    Mel_Geometry_Region* r = mel_slotmap_get(&pool->catalog, h.handle);
    assert(r != nullptr);

    return pool->vertices.cpu + (u64)r->vertex_offset * pool->vertices.stride;
}

const void* mel_geometry_pool_cpu_indices(Mel_Geometry_Pool* pool, Mel_Geometry_Handle h)
{
    assert(pool != nullptr);

    Mel_Geometry_Region* r = mel_slotmap_get(&pool->catalog, h.handle);
    assert(r != nullptr);

    if (r->index_count == 0)
        return nullptr;

    Mel_Geometry_Lane* lane = (r->index_type == MEL_GPU_INDEX_TYPE_U16)
        ? &pool->indices_u16
        : &pool->indices_u32;

    assert(lane->cpu != nullptr);

    u32 idx_stride = (r->index_type == MEL_GPU_INDEX_TYPE_U16) ? sizeof(u16) : sizeof(u32);
    return lane->cpu + (u64)r->index_offset * idx_stride;
}
