#pragma once

#include "render.manager.2d.fwd.h"
#include "gpu.storage_pool.h"
#include "gpu.device.fwd.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "collection.bitset.h"
#include "allocator.fwd.h"

#define MEL_RF2D_HIDDEN     (1u << 0)
#define MEL_RF2D_FLIP_X     (1u << 1)
#define MEL_RF2D_FLIP_Y     (1u << 2)

typedef struct {
    Mel_Vec2 pos;
    Mel_Vec2 scale;
    f32 rotation;
    f32 depth;
    u32 flags;
    u32 _pad;
} Mel_Render_Transform_2D;

typedef struct {
    Mel_Vec2 center;
    Mel_Vec2 half_extents;
} Mel_Render_Bounds_2D;

typedef struct {
    Mel_Rect uv;
    Mel_Vec4 color;
    u32 texture_idx;
    u32 material_base_id;
    u32 layer;
    u32 _pad;
} Mel_Render_Sprite_Info;

struct Mel_Render_Manager_2D {
    Mel_Storage_Pool transforms;
    Mel_Storage_Pool bounds;
    Mel_Storage_Pool sprite_infos;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    u32 initial_capacity;
} Mel_Render_Manager_2D_Opt;

void mel_mgr_2d_init_opt(Mel_Render_Manager_2D* mgr, Mel_Render_Manager_2D_Opt opt);
#define mel_mgr_2d_init(mgr, ...) mel_mgr_2d_init_opt((mgr), (Mel_Render_Manager_2D_Opt){__VA_ARGS__})

void mel_mgr_2d_shutdown(Mel_Render_Manager_2D* mgr);

Mel_Render_Handle_2D mel_mgr_2d_alloc(Mel_Render_Manager_2D* mgr);
void                 mel_mgr_2d_free(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h);

void mel_mgr_2d_set_transform(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h, Mel_Render_Transform_2D t);
void mel_mgr_2d_set_bounds(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h, Mel_Render_Bounds_2D b);
void mel_mgr_2d_set_sprite_info(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h, Mel_Render_Sprite_Info info);

void mel_mgr_2d_set_pos(Mel_Render_Manager_2D* mgr, Mel_Render_Handle_2D h, Mel_Vec2 pos);

void mel_mgr_2d_upload_dirty(Mel_Render_Manager_2D* mgr);

void mel_mgr_2d_cull_rect(Mel_Render_Manager_2D* mgr, Mel_Rect viewport, Mel_BitSet* out_visibility);

u32              mel_mgr_2d_count(Mel_Render_Manager_2D* mgr);
Mel_Gpu_Buffer*  mel_mgr_2d_transform_buffer(Mel_Render_Manager_2D* mgr);
Mel_Gpu_Buffer*  mel_mgr_2d_bounds_buffer(Mel_Render_Manager_2D* mgr);
Mel_Gpu_Buffer*  mel_mgr_2d_sprite_info_buffer(Mel_Render_Manager_2D* mgr);
