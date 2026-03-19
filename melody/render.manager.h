#pragma once

#include "render.manager.fwd.h"
#include "gpu.buffer.h"
#include "collection.bitset.h"
#include "gpu.device.fwd.h"
#include "allocator.fwd.h"

#define MEL_MGR_FREE_END UINT32_MAX

typedef struct {
    u8* data;
    Mel_BitSet dirty;
    Mel_Gpu_Buffer gpu;
    usize item_size;
    u32 gpu_capacity;
} Mel__Mgr_Pool;

struct Mel_Render_Manager {
    u32* sparse;
    u32* generations;
    u32* packed_to_sparse;
    u32* packed_groups;
    u32 sparse_capacity;
    u32 packed_count;
    u32 free_head;

    Mel__Mgr_Pool* pools;
    u32 pool_count;

    Mel_Mgr_Range* ranges;
    u32 range_count;
    u32 range_capacity;

    Mel_Gpu_Buffer draw_order;
    u32* draw_order_cpu;
    u32 draw_order_capacity;
    bool order_dirty;

    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
};

typedef struct {
    usize item_size;
} Mel_Mgr_Pool_Desc;

typedef struct {
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    const Mel_Mgr_Pool_Desc* pools;
    u32 pool_count;
    u32 initial_capacity;
} Mel_Render_Manager_Opt;

void mel_mgr_init_opt(Mel_Render_Manager* mgr, Mel_Render_Manager_Opt opt);
#define mel_mgr_init(mgr, ...) mel_mgr_init_opt((mgr), (Mel_Render_Manager_Opt){__VA_ARGS__})

void mel_mgr_shutdown(Mel_Render_Manager* mgr);

Mel_Render_Handle mel_mgr_alloc(Mel_Render_Manager* mgr, u32 group);
void              mel_mgr_free(Mel_Render_Manager* mgr, Mel_Render_Handle h);
bool              mel_mgr_alive(Mel_Render_Manager* mgr, Mel_Render_Handle h);

void  mel_mgr_set(Mel_Render_Manager* mgr, u32 pool, Mel_Render_Handle h, const void* data);
void* mel_mgr_get(Mel_Render_Manager* mgr, u32 pool, Mel_Render_Handle h);
void  mel_mgr_mark_dirty(Mel_Render_Manager* mgr, u32 pool, Mel_Render_Handle h);

void mel_mgr_change_group(Mel_Render_Manager* mgr, Mel_Render_Handle h, u32 new_group);

void mel_mgr_upload_dirty(Mel_Render_Manager* mgr);

u32              mel_mgr_count(Mel_Render_Manager* mgr);
const void*      mel_mgr_pool_data(Mel_Render_Manager* mgr, u32 pool);
Mel_Gpu_Buffer*  mel_mgr_pool_buffer(Mel_Render_Manager* mgr, u32 pool);
Mel_Gpu_Buffer*  mel_mgr_draw_order_buffer(Mel_Render_Manager* mgr);

const Mel_Mgr_Range* mel_mgr_group_ranges(Mel_Render_Manager* mgr);
u32                  mel_mgr_group_count(Mel_Render_Manager* mgr);
