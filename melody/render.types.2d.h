#pragma once

#include "core.types.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"

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
    Mel_Rect uv;
    Mel_Vec4 color;
    u32 texture_idx;
    u32 material_base_id;
    u32 layer;
    u32 _pad;
} Mel_Render_Sprite_Info;

_Static_assert(sizeof(Mel_Render_Transform_2D) == 32, "Mel_Render_Transform_2D must be 32 bytes");
_Static_assert(sizeof(Mel_Render_Sprite_Info) == 48, "Mel_Render_Sprite_Info must be 48 bytes");

#define MEL_2D_POOL_TRANSFORMS 0
#define MEL_2D_POOL_INFOS      1
#define MEL_2D_POOL_COUNT      2
