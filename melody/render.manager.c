#include "render.manager.h"
#include "gpu.device.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "render.material_base.h"

#include <string.h>
#include <assert.h>

static void mel__mgr_grow_sparse(Mel_Render_Manager* mgr, u32 needed)
{
    if (needed <= mgr->sparse_capacity)
        return;

    u32 new_cap = mgr->sparse_capacity == 0 ? 64 : mgr->sparse_capacity;
    while (new_cap < needed)
        new_cap *= 2;

    u32* new_sparse = mel_alloc(mgr->alloc, new_cap * sizeof(u32));
    u32* new_gens   = mel_alloc(mgr->alloc, new_cap * sizeof(u32));

    if (mgr->sparse_capacity > 0)
    {
        memcpy(new_sparse, mgr->sparse, mgr->sparse_capacity * sizeof(u32));
        memcpy(new_gens, mgr->generations, mgr->sparse_capacity * sizeof(u32));
    }

    for (u32 i = mgr->sparse_capacity; i < new_cap; i++)
    {
        new_sparse[i] = (i + 1 < new_cap) ? (i + 1) : MEL_MGR_FREE_END;
        new_gens[i] = 0;
    }

    if (mgr->free_head == MEL_MGR_FREE_END)
        mgr->free_head = mgr->sparse_capacity;
    else
    {
        u32 tail = mgr->free_head;
        while (mgr->sparse[tail] != MEL_MGR_FREE_END)
            tail = mgr->sparse[tail];
        mgr->sparse[tail] = mgr->sparse_capacity;
        memcpy(new_sparse, mgr->sparse, mgr->sparse_capacity * sizeof(u32));
        new_sparse[tail] = mgr->sparse_capacity;
    }

    if (mgr->sparse) mel_dealloc(mgr->alloc, mgr->sparse);
    if (mgr->generations) mel_dealloc(mgr->alloc, mgr->generations);

    mgr->sparse = new_sparse;
    mgr->generations = new_gens;
    mgr->sparse_capacity = new_cap;
}

static void mel__mgr_grow_packed(Mel_Render_Manager* mgr, u32 needed)
{
    for (u32 p = 0; p < mgr->pool_count; p++)
    {
        Mel__Mgr_Pool* pool = &mgr->pools[p];
        u32 current_cap = pool->gpu_capacity;
        if (needed <= current_cap)
            continue;

        u32 new_cap = current_cap == 0 ? 64 : current_cap;
        while (new_cap < needed)
            new_cap *= 2;

        u8* new_data = mel_alloc(mgr->alloc, (usize)new_cap * pool->item_size);
        if (pool->data)
        {
            memcpy(new_data, pool->data, (usize)mgr->packed_count * pool->item_size);
            mel_dealloc(mgr->alloc, pool->data);
        }
        pool->data = new_data;

        if (pool->gpu._handle != nullptr)
            mel_gpu_buffer_shutdown(&pool->gpu, mgr->dev);

        mel_gpu_buffer_init(&pool->gpu, mgr->dev,
            .size = (u64)new_cap * pool->item_size,
            .usage = MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU,
            .map_on_create = true);

        mel_bitset_resize(&pool->dirty, new_cap);
        pool->gpu_capacity = new_cap;
    }

    u32 pts_cap = mgr->pools[0].gpu_capacity;
    u32* new_pts = mel_alloc(mgr->alloc, pts_cap * sizeof(u32));
    u32* new_grp = mel_alloc(mgr->alloc, pts_cap * sizeof(u32));
    if (mgr->packed_count > 0)
    {
        memcpy(new_pts, mgr->packed_to_sparse, mgr->packed_count * sizeof(u32));
        memcpy(new_grp, mgr->packed_groups, mgr->packed_count * sizeof(u32));
    }
    if (mgr->packed_to_sparse) mel_dealloc(mgr->alloc, mgr->packed_to_sparse);
    if (mgr->packed_groups) mel_dealloc(mgr->alloc, mgr->packed_groups);
    mgr->packed_to_sparse = new_pts;
    mgr->packed_groups = new_grp;
}

void mel_mgr_init_opt(Mel_Render_Manager* mgr, Mel_Render_Manager_Opt opt)
{
    assert(mgr != nullptr);
    assert(opt.dev != nullptr);
    assert(opt.pools != nullptr);
    assert(opt.pool_count > 0);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    u32 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 64;

    *mgr = (Mel_Render_Manager){0};
    mgr->dev = opt.dev;
    mgr->alloc = alloc;
    mgr->free_head = MEL_MGR_FREE_END;
    mgr->pool_count = opt.pool_count;

    mgr->pools = mel_alloc(alloc, opt.pool_count * sizeof(Mel__Mgr_Pool));
    for (u32 p = 0; p < opt.pool_count; p++)
    {
        mgr->pools[p] = (Mel__Mgr_Pool){0};
        mgr->pools[p].item_size = opt.pools[p].item_size;
        mel_bitset_init(&mgr->pools[p].dirty, cap, alloc);
    }

    mel__mgr_grow_sparse(mgr, cap);
    mel__mgr_grow_packed(mgr, cap);
}

void mel_mgr_shutdown(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);

    for (u32 p = 0; p < mgr->pool_count; p++)
    {
        if (mgr->pools[p].gpu._handle != nullptr)
            mel_gpu_buffer_shutdown(&mgr->pools[p].gpu, mgr->dev);
        if (mgr->pools[p].data)
            mel_dealloc(mgr->alloc, mgr->pools[p].data);
        mel_bitset_free(&mgr->pools[p].dirty);
    }
    if (mgr->pools) mel_dealloc(mgr->alloc, mgr->pools);

    if (mgr->draw_order._handle != nullptr)
        mel_gpu_buffer_shutdown(&mgr->draw_order, mgr->dev);
    if (mgr->draw_order_cpu) mel_dealloc(mgr->alloc, mgr->draw_order_cpu);
    if (mgr->ranges) mel_dealloc(mgr->alloc, mgr->ranges);
    if (mgr->sparse) mel_dealloc(mgr->alloc, mgr->sparse);
    if (mgr->generations) mel_dealloc(mgr->alloc, mgr->generations);
    if (mgr->packed_to_sparse) mel_dealloc(mgr->alloc, mgr->packed_to_sparse);
    if (mgr->packed_groups) mel_dealloc(mgr->alloc, mgr->packed_groups);

    *mgr = (Mel_Render_Manager){0};
}

Mel_Render_Handle mel_mgr_alloc(Mel_Render_Manager* mgr, u32 group)
{
    assert(mgr != nullptr);

    if (mgr->free_head == MEL_MGR_FREE_END)
        mel__mgr_grow_sparse(mgr, mgr->sparse_capacity + 1);

    u32 idx = mgr->free_head;
    mgr->free_head = mgr->sparse[idx];

    u32 gen = ++mgr->generations[idx];

    u32 packed = mgr->packed_count;
    mel__mgr_grow_packed(mgr, packed + 1);

    mgr->sparse[idx] = packed;
    mgr->packed_to_sparse[packed] = idx;
    mgr->packed_groups[packed] = group;
    mgr->packed_count++;

    for (u32 p = 0; p < mgr->pool_count; p++)
    {
        memset(mgr->pools[p].data + (usize)packed * mgr->pools[p].item_size, 0, mgr->pools[p].item_size);
        mel_bitset_set(&mgr->pools[p].dirty, packed);
    }

    mgr->order_dirty = true;

    return (Mel_Render_Handle){ .idx = idx, .gen = gen };
}

bool mel_mgr_alive(Mel_Render_Manager* mgr, Mel_Render_Handle h)
{
    assert(mgr != nullptr);
    if (h.gen == 0 || h.idx >= mgr->sparse_capacity)
        return false;
    return mgr->generations[h.idx] == h.gen;
}

void mel_mgr_free(Mel_Render_Manager* mgr, Mel_Render_Handle h)
{
    assert(mgr != nullptr);
    assert(mel_mgr_alive(mgr, h));

    u32 packed = mgr->sparse[h.idx];
    u32 last = mgr->packed_count - 1;

    if (packed != last)
    {
        u32 last_sparse = mgr->packed_to_sparse[last];

        for (u32 p = 0; p < mgr->pool_count; p++)
        {
            Mel__Mgr_Pool* pool = &mgr->pools[p];
            memcpy(pool->data + (usize)packed * pool->item_size,
                   pool->data + (usize)last * pool->item_size,
                   pool->item_size);

            bool last_dirty = mel_bitset_get(&pool->dirty, last);
            mel_bitset_clear_bit(&pool->dirty, last);
            if (last_dirty)
                mel_bitset_set(&pool->dirty, packed);
            else
                mel_bitset_set(&pool->dirty, packed);
        }

        mgr->packed_to_sparse[packed] = last_sparse;
        mgr->packed_groups[packed] = mgr->packed_groups[last];
        mgr->sparse[last_sparse] = packed;
    }

    mgr->packed_count--;

    mgr->sparse[h.idx] = mgr->free_head;
    mgr->free_head = h.idx;

    mgr->order_dirty = true;
}

void mel_mgr_set(Mel_Render_Manager* mgr, u32 pool, Mel_Render_Handle h, const void* data)
{
    assert(mgr != nullptr);
    assert(pool < mgr->pool_count);
    assert(mel_mgr_alive(mgr, h));
    assert(data != nullptr);

    u32 packed = mgr->sparse[h.idx];
    Mel__Mgr_Pool* p = &mgr->pools[pool];
    memcpy(p->data + (usize)packed * p->item_size, data, p->item_size);
    mel_bitset_set(&p->dirty, packed);
}

void* mel_mgr_get(Mel_Render_Manager* mgr, u32 pool, Mel_Render_Handle h)
{
    assert(mgr != nullptr);
    assert(pool < mgr->pool_count);
    assert(mel_mgr_alive(mgr, h));

    u32 packed = mgr->sparse[h.idx];
    return mgr->pools[pool].data + (usize)packed * mgr->pools[pool].item_size;
}

void mel_mgr_mark_dirty(Mel_Render_Manager* mgr, u32 pool, Mel_Render_Handle h)
{
    assert(mgr != nullptr);
    assert(pool < mgr->pool_count);
    assert(mel_mgr_alive(mgr, h));

    u32 packed = mgr->sparse[h.idx];
    mel_bitset_set(&mgr->pools[pool].dirty, packed);
}

void mel_mgr_change_group(Mel_Render_Manager* mgr, Mel_Render_Handle h, u32 new_group)
{
    assert(mgr != nullptr);
    assert(mel_mgr_alive(mgr, h));

    u32 packed = mgr->sparse[h.idx];
    mgr->packed_groups[packed] = new_group;
    mgr->order_dirty = true;
}

static void mel__mgr_rebuild_draw_order(Mel_Render_Manager* mgr)
{
    u32 count = mgr->packed_count;
    if (count == 0)
    {
        mgr->range_count = 0;
        mgr->order_dirty = false;
        return;
    }

    if (count > mgr->draw_order_capacity)
    {
        u32 new_cap = mgr->draw_order_capacity == 0 ? 64 : mgr->draw_order_capacity;
        while (new_cap < count)
            new_cap *= 2;

        if (mgr->draw_order_cpu)
            mel_dealloc(mgr->alloc, mgr->draw_order_cpu);
        mgr->draw_order_cpu = mel_alloc(mgr->alloc, new_cap * sizeof(u32));
        mgr->draw_order_capacity = new_cap;
    }

    u32 max_group = 0;
    for (u32 i = 0; i < count; i++)
    {
        if (mgr->packed_groups[i] > max_group)
            max_group = mgr->packed_groups[i];
    }

    u32 bucket_count = max_group + 1;
    assert(bucket_count <= MEL_MATERIAL_BASE_MAX);

    u32 counts[MEL_MATERIAL_BASE_MAX] = {0};
    u32 offsets[MEL_MATERIAL_BASE_MAX] = {0};

    for (u32 i = 0; i < count; i++)
        counts[mgr->packed_groups[i]]++;

    u32 offset = 0;
    for (u32 b = 0; b < bucket_count; b++)
    {
        offsets[b] = offset;
        offset += counts[b];
    }

    u32 write_pos[MEL_MATERIAL_BASE_MAX];
    memcpy(write_pos, offsets, bucket_count * sizeof(u32));

    for (u32 i = 0; i < count; i++)
    {
        u32 g = mgr->packed_groups[i];
        mgr->draw_order_cpu[write_pos[g]++] = i;
    }

    u32 needed_ranges = 0;
    for (u32 b = 0; b < bucket_count; b++)
    {
        if (counts[b] > 0)
            needed_ranges++;
    }

    if (needed_ranges > mgr->range_capacity)
    {
        if (mgr->ranges)
            mel_dealloc(mgr->alloc, mgr->ranges);
        mgr->range_capacity = needed_ranges;
        mgr->ranges = mel_alloc(mgr->alloc, needed_ranges * sizeof(Mel_Mgr_Range));
    }

    mgr->range_count = 0;
    for (u32 b = 0; b < bucket_count; b++)
    {
        if (counts[b] > 0)
        {
            mgr->ranges[mgr->range_count++] = (Mel_Mgr_Range){
                .group = b,
                .start = offsets[b],
                .count = counts[b],
            };
        }
    }

    u64 buf_size = (u64)count * sizeof(u32);

    if (mgr->draw_order._handle == nullptr || mgr->draw_order.size < buf_size)
    {
        if (mgr->draw_order._handle != nullptr)
            mel_gpu_buffer_shutdown(&mgr->draw_order, mgr->dev);

        u64 alloc_size = buf_size;
        if (alloc_size < 256) alloc_size = 256;

        mel_gpu_buffer_init(&mgr->draw_order, mgr->dev,
            .size = alloc_size,
            .usage = MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU,
            .map_on_create = true);
    }

    mel_gpu_buffer_upload(&mgr->draw_order, mgr->dev, mgr->draw_order_cpu, buf_size, 0);
    mgr->order_dirty = false;
}

void mel_mgr_upload_dirty(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);

    for (u32 p = 0; p < mgr->pool_count; p++)
    {
        Mel__Mgr_Pool* pool = &mgr->pools[p];
        if (!mel_bitset_any(&pool->dirty))
            continue;

        for (u32 i = 0; i < mgr->packed_count; i++)
        {
            if (!mel_bitset_get(&pool->dirty, i))
                continue;

            mel_gpu_buffer_upload(&pool->gpu, mgr->dev,
                pool->data + (usize)i * pool->item_size,
                (u64)pool->item_size,
                (u64)i * pool->item_size);
        }

        mel_bitset_clear(&pool->dirty);
    }

    if (mgr->order_dirty)
        mel__mgr_rebuild_draw_order(mgr);
}

u32 mel_mgr_count(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mgr->packed_count;
}

const void* mel_mgr_pool_data(Mel_Render_Manager* mgr, u32 pool)
{
    assert(mgr != nullptr);
    assert(pool < mgr->pool_count);
    return mgr->pools[pool].data;
}

Mel_Gpu_Buffer* mel_mgr_pool_buffer(Mel_Render_Manager* mgr, u32 pool)
{
    assert(mgr != nullptr);
    assert(pool < mgr->pool_count);
    return &mgr->pools[pool].gpu;
}

Mel_Gpu_Buffer* mel_mgr_draw_order_buffer(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return &mgr->draw_order;
}

const Mel_Mgr_Range* mel_mgr_group_ranges(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mgr->ranges;
}

u32 mel_mgr_group_count(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mgr->range_count;
}
