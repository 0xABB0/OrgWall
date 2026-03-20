#include "render.manager.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <assert.h>
#include <string.h>

static void mel__mgr_grow_sparse(Mel_Render_Manager* mgr, u32 needed)
{
    if (needed <= mgr->sparse_capacity)
        return;

    u32 new_cap = mgr->sparse_capacity == 0 ? 64 : mgr->sparse_capacity;
    while (new_cap < needed)
        new_cap *= 2;

    u32* new_sparse = mel_alloc(mgr->alloc, new_cap * sizeof(u32));
    u32* new_gens = mel_alloc(mgr->alloc, new_cap * sizeof(u32));

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
    if (needed <= mgr->packed_capacity)
        return;

    u32 new_cap = mgr->packed_capacity == 0 ? 64 : mgr->packed_capacity;
    while (new_cap < needed)
        new_cap *= 2;

    Mel_Render_Instance* new_instances = mel_alloc(mgr->alloc, (usize)new_cap * sizeof(Mel_Render_Instance));
    u32* new_pts = mel_alloc(mgr->alloc, new_cap * sizeof(u32));

    if (mgr->packed_count > 0)
    {
        memcpy(new_instances, mgr->instances, (usize)mgr->packed_count * sizeof(Mel_Render_Instance));
        memcpy(new_pts, mgr->packed_to_sparse, mgr->packed_count * sizeof(u32));
    }

    if (mgr->instances) mel_dealloc(mgr->alloc, mgr->instances);
    if (mgr->packed_to_sparse) mel_dealloc(mgr->alloc, mgr->packed_to_sparse);

    mgr->instances = new_instances;
    mgr->packed_to_sparse = new_pts;
    mel_bitset_resize(&mgr->dirty, new_cap);
    mgr->packed_capacity = new_cap;
}

void mel_mgr_init_opt(Mel_Render_Manager* mgr, Mel_Render_Manager_Opt opt)
{
    assert(mgr != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    u32 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 64;

    *mgr = (Mel_Render_Manager){0};
    mgr->alloc = alloc;
    mgr->free_head = MEL_MGR_FREE_END;
    mgr->mutation_serial = 1;

    mel_bitset_init(&mgr->dirty, cap, alloc);
    mel__mgr_grow_sparse(mgr, cap);
    mel__mgr_grow_packed(mgr, cap);
}

void mel_mgr_shutdown(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);

    if (mgr->instances) mel_dealloc(mgr->alloc, mgr->instances);
    if (mgr->sparse) mel_dealloc(mgr->alloc, mgr->sparse);
    if (mgr->generations) mel_dealloc(mgr->alloc, mgr->generations);
    if (mgr->packed_to_sparse) mel_dealloc(mgr->alloc, mgr->packed_to_sparse);
    mel_bitset_free(&mgr->dirty);

    *mgr = (Mel_Render_Manager){0};
}

Mel_Render_Handle mel_mgr_alloc(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);

    if (mgr->free_head == MEL_MGR_FREE_END)
        mel__mgr_grow_sparse(mgr, mgr->sparse_capacity + 1);

    if (mgr->packed_count >= mgr->packed_capacity)
        mel__mgr_grow_packed(mgr, mgr->packed_count + 1);

    u32 idx = mgr->free_head;
    mgr->free_head = mgr->sparse[idx];

    u32 gen = ++mgr->generations[idx];
    u32 packed = mgr->packed_count++;

    mgr->sparse[idx] = packed;
    mgr->packed_to_sparse[packed] = idx;
    mgr->instances[packed] = (Mel_Render_Instance){0};
    mel_bitset_set(&mgr->dirty, packed);
    mgr->mutation_serial++;

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
        mgr->instances[packed] = mgr->instances[last];
        mgr->packed_to_sparse[packed] = last_sparse;
        mgr->sparse[last_sparse] = packed;
        mel_bitset_set(&mgr->dirty, packed);
    }

    mel_bitset_clear_bit(&mgr->dirty, last);
    mgr->packed_count--;

    mgr->sparse[h.idx] = mgr->free_head;
    mgr->free_head = h.idx;
    mgr->mutation_serial++;
}

void mel_mgr_set_instance(Mel_Render_Manager* mgr, Mel_Render_Handle h, const Mel_Render_Instance* instance)
{
    assert(mgr != nullptr);
    assert(mel_mgr_alive(mgr, h));
    assert(instance != nullptr);

    u32 packed = mgr->sparse[h.idx];
    mgr->instances[packed] = *instance;
    mel_bitset_set(&mgr->dirty, packed);
    mgr->mutation_serial++;
}

Mel_Render_Instance* mel_mgr_get_instance(Mel_Render_Manager* mgr, Mel_Render_Handle h)
{
    assert(mgr != nullptr);
    assert(mel_mgr_alive(mgr, h));

    u32 packed = mgr->sparse[h.idx];
    return &mgr->instances[packed];
}

void mel_mgr_mark_dirty(Mel_Render_Manager* mgr, Mel_Render_Handle h)
{
    assert(mgr != nullptr);
    assert(mel_mgr_alive(mgr, h));

    u32 packed = mgr->sparse[h.idx];
    mel_bitset_set(&mgr->dirty, packed);
    mgr->mutation_serial++;
}

u32 mel_mgr_count(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mgr->packed_count;
}

const Mel_Render_Instance* mel_mgr_instances(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mgr->instances;
}

u64 mel_mgr_mutation_serial(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);
    return mgr->mutation_serial;
}
