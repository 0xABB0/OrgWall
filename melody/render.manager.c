#include "render.manager.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <assert.h>
#include <string.h>

static void mel__mgr_space_shutdown(Mel_Render_Manager* mgr, Mel_Render_Space* space)
{
    if (space->payload == nullptr)
        return;

    if (space->type != nullptr && space->type->shutdown_payload != nullptr)
        space->type->shutdown_payload(space->payload, mgr->alloc);
    mel_dealloc(mgr->alloc, space->payload);
    *space = (Mel_Render_Space){0};
}

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

static void mel__mgr_grow_space_sparse(Mel_Render_Manager* mgr, u32 needed)
{
    if (needed <= mgr->space_sparse_capacity)
        return;

    u32 new_cap = mgr->space_sparse_capacity == 0 ? 64 : mgr->space_sparse_capacity;
    while (new_cap < needed)
        new_cap *= 2;

    u32* new_sparse = mel_alloc(mgr->alloc, new_cap * sizeof(u32));
    u32* new_gens = mel_alloc(mgr->alloc, new_cap * sizeof(u32));

    if (mgr->space_sparse_capacity > 0)
    {
        memcpy(new_sparse, mgr->space_sparse, mgr->space_sparse_capacity * sizeof(u32));
        memcpy(new_gens, mgr->space_generations, mgr->space_sparse_capacity * sizeof(u32));
    }

    for (u32 i = mgr->space_sparse_capacity; i < new_cap; i++)
    {
        new_sparse[i] = (i + 1 < new_cap) ? (i + 1) : MEL_MGR_FREE_END;
        new_gens[i] = 0;
    }

    if (mgr->space_free_head == MEL_MGR_FREE_END)
        mgr->space_free_head = mgr->space_sparse_capacity;
    else
    {
        u32 tail = mgr->space_free_head;
        while (mgr->space_sparse[tail] != MEL_MGR_FREE_END)
            tail = mgr->space_sparse[tail];
        new_sparse[tail] = mgr->space_sparse_capacity;
    }

    if (mgr->space_sparse) mel_dealloc(mgr->alloc, mgr->space_sparse);
    if (mgr->space_generations) mel_dealloc(mgr->alloc, mgr->space_generations);

    mgr->space_sparse = new_sparse;
    mgr->space_generations = new_gens;
    mgr->space_sparse_capacity = new_cap;
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
    Mel_Render_Material_Binding** new_instance_material_bindings = mel_alloc(mgr->alloc, new_cap * sizeof(Mel_Render_Material_Binding*));
    u32* new_instance_material_binding_counts = mel_alloc(mgr->alloc, new_cap * sizeof(u32));
    u32* new_instance_material_binding_capacities = mel_alloc(mgr->alloc, new_cap * sizeof(u32));

    if (mgr->packed_count > 0)
    {
        memcpy(new_instances, mgr->instances, (usize)mgr->packed_count * sizeof(Mel_Render_Instance));
        memcpy(new_pts, mgr->packed_to_sparse, mgr->packed_count * sizeof(u32));
        memcpy(new_instance_material_bindings, mgr->instance_material_bindings,
               mgr->packed_count * sizeof(Mel_Render_Material_Binding*));
        memcpy(new_instance_material_binding_counts, mgr->instance_material_binding_counts,
               mgr->packed_count * sizeof(u32));
        memcpy(new_instance_material_binding_capacities, mgr->instance_material_binding_capacities,
               mgr->packed_count * sizeof(u32));
    }

    for (u32 i = mgr->packed_count; i < new_cap; i++)
    {
        new_instance_material_bindings[i] = nullptr;
        new_instance_material_binding_counts[i] = 0;
        new_instance_material_binding_capacities[i] = 0;
    }

    if (mgr->instances) mel_dealloc(mgr->alloc, mgr->instances);
    if (mgr->packed_to_sparse) mel_dealloc(mgr->alloc, mgr->packed_to_sparse);
    if (mgr->instance_material_bindings) mel_dealloc(mgr->alloc, mgr->instance_material_bindings);
    if (mgr->instance_material_binding_counts) mel_dealloc(mgr->alloc, mgr->instance_material_binding_counts);
    if (mgr->instance_material_binding_capacities) mel_dealloc(mgr->alloc, mgr->instance_material_binding_capacities);

    mgr->instances = new_instances;
    mgr->packed_to_sparse = new_pts;
    mgr->instance_material_bindings = new_instance_material_bindings;
    mgr->instance_material_binding_counts = new_instance_material_binding_counts;
    mgr->instance_material_binding_capacities = new_instance_material_binding_capacities;
    mel_bitset_resize(&mgr->dirty, new_cap);
    mgr->packed_capacity = new_cap;
}

static void mel__mgr_grow_space_packed(Mel_Render_Manager* mgr, u32 needed)
{
    if (needed <= mgr->space_packed_capacity)
        return;

    u32 new_cap = mgr->space_packed_capacity == 0 ? 64 : mgr->space_packed_capacity;
    while (new_cap < needed)
        new_cap *= 2;

    Mel_Render_Space* new_spaces = mel_alloc(mgr->alloc, (usize)new_cap * sizeof(Mel_Render_Space));
    u32* new_pts = mel_alloc(mgr->alloc, new_cap * sizeof(u32));

    if (mgr->space_packed_count > 0)
    {
        memcpy(new_spaces, mgr->spaces, (usize)mgr->space_packed_count * sizeof(Mel_Render_Space));
        memcpy(new_pts, mgr->space_packed_to_sparse, mgr->space_packed_count * sizeof(u32));
    }

    for (u32 i = mgr->space_packed_count; i < new_cap; i++)
        new_spaces[i] = (Mel_Render_Space){0};

    if (mgr->spaces) mel_dealloc(mgr->alloc, mgr->spaces);
    if (mgr->space_packed_to_sparse) mel_dealloc(mgr->alloc, mgr->space_packed_to_sparse);

    mgr->spaces = new_spaces;
    mgr->space_packed_to_sparse = new_pts;
    mgr->space_packed_capacity = new_cap;
}

void mel_mgr_init_opt(Mel_Render_Manager* mgr, Mel_Render_Manager_Opt opt)
{
    assert(mgr != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    u32 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 64;

    *mgr = (Mel_Render_Manager){0};
    mgr->alloc = alloc;
    mgr->free_head = MEL_MGR_FREE_END;
    mgr->space_free_head = MEL_MGR_FREE_END;
    mgr->mutation_serial = 1;

    mel_bitset_init(&mgr->dirty, cap, alloc);
    mel__mgr_grow_sparse(mgr, cap);
    mel__mgr_grow_packed(mgr, cap);
    mel__mgr_grow_space_sparse(mgr, cap);
    mel__mgr_grow_space_packed(mgr, cap);
}

void mel_mgr_shutdown(Mel_Render_Manager* mgr)
{
    assert(mgr != nullptr);

    for (u32 i = 0; i < mgr->packed_count; i++)
        if (mgr->instance_material_bindings && mgr->instance_material_bindings[i])
            mel_dealloc(mgr->alloc, mgr->instance_material_bindings[i]);
    for (u32 i = 0; i < mgr->space_packed_count; i++)
        mel__mgr_space_shutdown(mgr, &mgr->spaces[i]);

    if (mgr->instances) mel_dealloc(mgr->alloc, mgr->instances);
    if (mgr->sparse) mel_dealloc(mgr->alloc, mgr->sparse);
    if (mgr->generations) mel_dealloc(mgr->alloc, mgr->generations);
    if (mgr->packed_to_sparse) mel_dealloc(mgr->alloc, mgr->packed_to_sparse);
    if (mgr->instance_material_bindings) mel_dealloc(mgr->alloc, mgr->instance_material_bindings);
    if (mgr->instance_material_binding_counts) mel_dealloc(mgr->alloc, mgr->instance_material_binding_counts);
    if (mgr->instance_material_binding_capacities) mel_dealloc(mgr->alloc, mgr->instance_material_binding_capacities);
    if (mgr->space_sparse) mel_dealloc(mgr->alloc, mgr->space_sparse);
    if (mgr->space_generations) mel_dealloc(mgr->alloc, mgr->space_generations);
    if (mgr->space_packed_to_sparse) mel_dealloc(mgr->alloc, mgr->space_packed_to_sparse);
    if (mgr->spaces) mel_dealloc(mgr->alloc, mgr->spaces);
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
    mgr->instance_material_bindings[packed] = nullptr;
    mgr->instance_material_binding_counts[packed] = 0;
    mgr->instance_material_binding_capacities[packed] = 0;
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

bool mel_render_space_handle_valid(Mel_Render_Space_Handle h)
{
    return h.gen != 0;
}

bool mel_mgr_space_alive(Mel_Render_Manager* mgr, Mel_Render_Space_Handle h)
{
    assert(mgr != nullptr);
    if (h.gen == 0 || h.idx >= mgr->space_sparse_capacity)
        return false;
    return mgr->space_generations[h.idx] == h.gen;
}

Mel_Render_Space_Handle mel_mgr_space_alloc(Mel_Render_Manager* mgr, const Mel_Render_Space_Type* type)
{
    assert(mgr != nullptr);
    assert(type != nullptr);

    if (mgr->space_free_head == MEL_MGR_FREE_END)
        mel__mgr_grow_space_sparse(mgr, mgr->space_sparse_capacity + 1);

    if (mgr->space_packed_count >= mgr->space_packed_capacity)
        mel__mgr_grow_space_packed(mgr, mgr->space_packed_count + 1);

    u32 idx = mgr->space_free_head;
    mgr->space_free_head = mgr->space_sparse[idx];

    u32 gen = ++mgr->space_generations[idx];
    u32 packed = mgr->space_packed_count++;

    mgr->space_sparse[idx] = packed;
    mgr->space_packed_to_sparse[packed] = idx;
    mgr->spaces[packed] = (Mel_Render_Space){
        .type = type,
        .payload = type->payload_size > 0 ? mel_alloc(mgr->alloc, type->payload_size) : nullptr,
    };
    if (mgr->spaces[packed].payload != nullptr)
        memset(mgr->spaces[packed].payload, 0, type->payload_size);
    mgr->mutation_serial++;

    return (Mel_Render_Space_Handle){ .idx = idx, .gen = gen };
}

void mel_mgr_space_free(Mel_Render_Manager* mgr, Mel_Render_Space_Handle h)
{
    assert(mgr != nullptr);
    assert(mel_mgr_space_alive(mgr, h));

    u32 packed = mgr->space_sparse[h.idx];
    u32 last = mgr->space_packed_count - 1;
    Mel_Render_Space removed_space = mgr->spaces[packed];

    if (packed != last)
    {
        u32 last_sparse = mgr->space_packed_to_sparse[last];
        mgr->spaces[packed] = mgr->spaces[last];
        mgr->space_packed_to_sparse[packed] = last_sparse;
        mgr->space_sparse[last_sparse] = packed;
    }

    mel__mgr_space_shutdown(mgr, &removed_space);
    mgr->spaces[last] = (Mel_Render_Space){0};
    mgr->space_packed_count--;

    mgr->space_sparse[h.idx] = mgr->space_free_head;
    mgr->space_free_head = h.idx;
    mgr->mutation_serial++;
}

void* mel_mgr_space_payload(Mel_Render_Manager* mgr, Mel_Render_Space_Handle h,
                            const Mel_Render_Space_Type* expected_type)
{
    assert(mgr != nullptr);
    assert(mel_mgr_space_alive(mgr, h));

    u32 packed = mgr->space_sparse[h.idx];
    if (expected_type != nullptr)
        assert(mgr->spaces[packed].type == expected_type);
    return mgr->spaces[packed].payload;
}

void mel_mgr_free(Mel_Render_Manager* mgr, Mel_Render_Handle h)
{
    assert(mgr != nullptr);
    assert(mel_mgr_alive(mgr, h));

    u32 packed = mgr->sparse[h.idx];
    u32 last = mgr->packed_count - 1;
    Mel_Render_Material_Binding* removed_bindings = mgr->instance_material_bindings[packed];

    if (packed != last)
    {
        u32 last_sparse = mgr->packed_to_sparse[last];
        mgr->instances[packed] = mgr->instances[last];
        mgr->packed_to_sparse[packed] = last_sparse;
        mgr->instance_material_bindings[packed] = mgr->instance_material_bindings[last];
        mgr->instance_material_binding_counts[packed] = mgr->instance_material_binding_counts[last];
        mgr->instance_material_binding_capacities[packed] = mgr->instance_material_binding_capacities[last];
        mgr->sparse[last_sparse] = packed;
        mel_bitset_set(&mgr->dirty, packed);
    }

    if (removed_bindings != nullptr)
        mel_dealloc(mgr->alloc, removed_bindings);
    mgr->instance_material_bindings[last] = nullptr;
    mgr->instance_material_binding_counts[last] = 0;
    mgr->instance_material_binding_capacities[last] = 0;
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
    assert(!mel_render_space_handle_valid(instance->space) || mel_mgr_space_alive(mgr, instance->space));

    u32 packed = mgr->sparse[h.idx];
    mgr->instances[packed] = *instance;
    mgr->instances[packed].material_binding_count = mgr->instance_material_binding_counts[packed];
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

void mel_mgr_set_material_bindings(Mel_Render_Manager* mgr, Mel_Render_Handle h,
                                   const Mel_Render_Material_Binding* bindings, u32 binding_count)
{
    assert(mgr != nullptr);
    assert(mel_mgr_alive(mgr, h));
    assert(binding_count == 0 || bindings != nullptr);

    u32 packed = mgr->sparse[h.idx];
    if (binding_count > mgr->instance_material_binding_capacities[packed])
    {
        u32 new_capacity = mgr->instance_material_binding_capacities[packed]
            ? mgr->instance_material_binding_capacities[packed]
            : 1;
        while (new_capacity < binding_count)
            new_capacity *= 2;

        mgr->instance_material_bindings[packed] = mgr->instance_material_bindings[packed]
            ? mel_realloc(mgr->alloc, mgr->instance_material_bindings[packed],
                          (usize)new_capacity * sizeof(Mel_Render_Material_Binding))
            : mel_alloc(mgr->alloc, (usize)new_capacity * sizeof(Mel_Render_Material_Binding));
        mgr->instance_material_binding_capacities[packed] = new_capacity;
    }

    if (binding_count > 0)
        memcpy(mgr->instance_material_bindings[packed], bindings,
               (usize)binding_count * sizeof(Mel_Render_Material_Binding));

    mgr->instance_material_binding_counts[packed] = binding_count;
    mgr->instances[packed].material_binding_count = binding_count;
    mel_bitset_set(&mgr->dirty, packed);
    mgr->mutation_serial++;
}

const Mel_Render_Material_Binding* mel_mgr_get_material_bindings(Mel_Render_Manager* mgr, Mel_Render_Handle h,
                                                                 u32* binding_count)
{
    assert(mgr != nullptr);
    assert(mel_mgr_alive(mgr, h));

    u32 packed = mgr->sparse[h.idx];
    if (binding_count != nullptr)
        *binding_count = mgr->instance_material_binding_counts[packed];
    return mgr->instance_material_bindings[packed];
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
