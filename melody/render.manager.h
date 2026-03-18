#pragma once

#include "render.manager.fwd.h"
#include "gpu.storage_pool.h"
#include "gpu.geometry_pool.fwd.h"
#include "gpu.device.fwd.h"
#include "render.cull.h"
#include "math.mat4.h"
#include "allocator.fwd.h"

typedef struct Mel_Material_Table Mel_Material_Table;

#define MEL_MGR_TRANSFORMS  (1u << 0)
#define MEL_MGR_BOUNDS      (1u << 1)
#define MEL_MGR_INFOS       (1u << 2)

#define MEL_RF_CAST_SHADOW  (1u << 0)
#define MEL_RF_STATIC       (1u << 1)
#define MEL_RF_HIDDEN       (1u << 2)

typedef struct {
    Mel_Vec3 center;
    Mel_Vec3 extents;
} Mel_Render_Bounds;

typedef struct {
    u32 material_base_id;
    u32 material_idx;
    Mel_Geometry_Handle mesh;
    u32 flags;
    u32 layer_mask;
    u32 _pad[2];
} Mel_Render_Info;

_Static_assert(sizeof(Mel_Render_Bounds) == 32, "Mel_Render_Bounds must be 32 bytes (two Mel_Vec3 at 16 bytes each)");
_Static_assert(sizeof(Mel_Render_Info) == 32, "Mel_Render_Info must be 32 bytes");

typedef struct {
    Mel_Mat4 model;
    Mel_Mat4 model_inverse;
} Mel_Render_Transform;

_Static_assert(sizeof(Mel_Render_Transform) == 128, "Mel_Render_Transform must be 128 bytes (two mat4)");

struct Mel_Render_Manager {
    Mel_Storage_Pool transforms;
    Mel_Storage_Pool bounds;
    Mel_Storage_Pool infos;

    Mel_Geometry_Pool* geometry;
    Mel_Material_Table* materials;

    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    u32 initial_capacity;
    u32 cpu_access_mask;
    Mel_Geometry_Pool* geometry;
    Mel_Material_Table* materials;
} Mel_Render_Manager_Opt;

void mel_mgr_init_opt(Mel_Render_Manager* mgr, Mel_Render_Manager_Opt opt);
#define mel_mgr_init(mgr, ...) mel_mgr_init_opt((mgr), (Mel_Render_Manager_Opt){__VA_ARGS__})

void mel_mgr_shutdown(Mel_Render_Manager* mgr);

Mel_Render_Handle mel_mgr_alloc(Mel_Render_Manager* mgr);
void              mel_mgr_free(Mel_Render_Manager* mgr, Mel_Render_Handle h);

void mel_mgr_set_transform(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Mat4 model);
void mel_mgr_set_bounds(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Render_Bounds bounds);
void mel_mgr_set_info(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Render_Info info);

void mel_mgr_upload_dirty(Mel_Render_Manager* mgr);

void mel_mgr_cull(Mel_Render_Manager* mgr, const Mel_Frustum* frustum, Mel_BitSet* out_visibility);

u32              mel_mgr_count(Mel_Render_Manager* mgr);
Mel_Gpu_Buffer*  mel_mgr_transform_buffer(Mel_Render_Manager* mgr);
Mel_Gpu_Buffer*  mel_mgr_bounds_buffer(Mel_Render_Manager* mgr);
Mel_Gpu_Buffer*  mel_mgr_info_buffer(Mel_Render_Manager* mgr);

void mel_mgr_request_cpu_access(Mel_Render_Manager* mgr, u32 buffer_mask);
