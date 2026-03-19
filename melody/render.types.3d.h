#pragma once

#include "core.types.h"
#include "math.mat4.h"
#include "math.vec3.h"
#include "gpu.geometry_pool.fwd.h"

#define MEL_RF_CAST_SHADOW  (1u << 0)
#define MEL_RF_STATIC       (1u << 1)
#define MEL_RF_HIDDEN       (1u << 2)

typedef struct {
    Mel_Mat4 model;
    Mel_Mat4 model_inverse;
} Mel_Render_Transform;

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

_Static_assert(sizeof(Mel_Render_Transform) == 128, "Mel_Render_Transform must be 128 bytes (two mat4)");
_Static_assert(sizeof(Mel_Render_Bounds) == 32, "Mel_Render_Bounds must be 32 bytes");
_Static_assert(sizeof(Mel_Render_Info) == 32, "Mel_Render_Info must be 32 bytes");

#define MEL_3D_POOL_TRANSFORMS 0
#define MEL_3D_POOL_BOUNDS     1
#define MEL_3D_POOL_INFOS      2
#define MEL_3D_POOL_COUNT      3
