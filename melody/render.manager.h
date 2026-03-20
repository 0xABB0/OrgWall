#pragma once

#include "render.manager.fwd.h"
#include "collection.bitset.h"
#include "allocator.fwd.h"

typedef struct Mel_Render_Source Mel_Render_Source;

#define MEL_MGR_FREE_END 0xFFFFFFFFu

typedef struct Mel_Render_Instance {
    Mel_Render_Source* source;
    u32 material_base_id;
    u32 material_idx;
    u32 flags;
    u32 visibility_mask;
} Mel_Render_Instance;

struct Mel_Render_Manager {
    u32* sparse;
    u32* generations;
    u32* packed_to_sparse;
    u32 sparse_capacity;
    u32 packed_capacity;
    u32 packed_count;
    u32 free_head;

    Mel_Render_Instance* instances;
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

void                  mel_mgr_set_instance(Mel_Render_Manager* mgr, Mel_Render_Handle h, const Mel_Render_Instance* instance);
Mel_Render_Instance*  mel_mgr_get_instance(Mel_Render_Manager* mgr, Mel_Render_Handle h);
void                  mel_mgr_mark_dirty(Mel_Render_Manager* mgr, Mel_Render_Handle h);

u32                          mel_mgr_count(Mel_Render_Manager* mgr);
const Mel_Render_Instance*   mel_mgr_instances(Mel_Render_Manager* mgr);
u64                          mel_mgr_mutation_serial(Mel_Render_Manager* mgr);
