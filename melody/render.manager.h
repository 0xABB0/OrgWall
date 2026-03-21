#pragma once

#include "render.manager.fwd.h"
#include "collection.bitset.h"
#include "allocator.fwd.h"

typedef struct Mel_Render_Source Mel_Render_Source;

#define MEL_MGR_FREE_END 0xFFFFFFFFu

typedef struct {
    u32 idx;
    u32 gen;
} Mel_Render_Space_Handle;

#define MEL_RENDER_SPACE_NONE ((Mel_Render_Space_Handle){0})

typedef struct Mel_Render_Space_Type {
    usize payload_size;
    void (*shutdown_payload)(void* payload, const Mel_Alloc* alloc);
} Mel_Render_Space_Type;

typedef struct {
    const Mel_Render_Space_Type* type;
    void* payload;
} Mel_Render_Space;

typedef struct Mel_Render_Instance {
    Mel_Render_Source* source;
    Mel_Render_Space_Handle space;
    u32 flags;
    u32 visibility_mask;
    u32 material_binding_count;
} Mel_Render_Instance;

typedef struct Mel_Render_Material_Binding {
    u32 slot;
    u32 material_base_id;
    u32 material_idx;
    u32 flags;
} Mel_Render_Material_Binding;

#define MEL_RENDER_MATERIAL_DOUBLE_SIDED (1u << 0)

struct Mel_Render_Manager {
    u32* sparse;
    u32* generations;
    u32* packed_to_sparse;
    u32 sparse_capacity;
    u32 packed_capacity;
    u32 packed_count;
    u32 free_head;

    Mel_Render_Instance* instances;
    Mel_Render_Material_Binding** instance_material_bindings;
    u32* instance_material_binding_counts;
    u32* instance_material_binding_capacities;

    u32* space_sparse;
    u32* space_generations;
    u32* space_packed_to_sparse;
    u32 space_sparse_capacity;
    u32 space_packed_capacity;
    u32 space_packed_count;
    u32 space_free_head;
    Mel_Render_Space* spaces;

    Mel_BitSet dirty;
    u64 mutation_serial;

    const Mel_Alloc* alloc;
};

typedef struct {
    const Mel_Alloc* alloc;
    u32 initial_capacity;
} Mel_Render_Manager_Opt;

void mel_mgr_init_opt(Mel_Render_Manager* mgr, Mel_Render_Manager_Opt opt);
#define mel_mgr_init(mgr, ...) mel_mgr_init_opt((mgr), (Mel_Render_Manager_Opt){__VA_ARGS__})

void mel_mgr_shutdown(Mel_Render_Manager* mgr);

Mel_Render_Handle mel_mgr_alloc(Mel_Render_Manager* mgr);
void              mel_mgr_free(Mel_Render_Manager* mgr, Mel_Render_Handle h);
bool              mel_mgr_alive(Mel_Render_Manager* mgr, Mel_Render_Handle h);
bool              mel_render_space_handle_valid(Mel_Render_Space_Handle h);
Mel_Render_Space_Handle mel_mgr_space_alloc(Mel_Render_Manager* mgr, const Mel_Render_Space_Type* type);
void              mel_mgr_space_free(Mel_Render_Manager* mgr, Mel_Render_Space_Handle h);
bool              mel_mgr_space_alive(Mel_Render_Manager* mgr, Mel_Render_Space_Handle h);
void*             mel_mgr_space_payload(Mel_Render_Manager* mgr, Mel_Render_Space_Handle h,
                                        const Mel_Render_Space_Type* expected_type);

void                  mel_mgr_set_instance(Mel_Render_Manager* mgr, Mel_Render_Handle h, const Mel_Render_Instance* instance);
Mel_Render_Instance*  mel_mgr_get_instance(Mel_Render_Manager* mgr, Mel_Render_Handle h);
void                  mel_mgr_set_material_bindings(Mel_Render_Manager* mgr, Mel_Render_Handle h,
                                                    const Mel_Render_Material_Binding* bindings, u32 binding_count);
const Mel_Render_Material_Binding* mel_mgr_get_material_bindings(Mel_Render_Manager* mgr, Mel_Render_Handle h,
                                                                 u32* binding_count);
void                  mel_mgr_mark_dirty(Mel_Render_Manager* mgr, Mel_Render_Handle h);

u32                          mel_mgr_count(Mel_Render_Manager* mgr);
const Mel_Render_Instance*   mel_mgr_instances(Mel_Render_Manager* mgr);
u64                          mel_mgr_mutation_serial(Mel_Render_Manager* mgr);
