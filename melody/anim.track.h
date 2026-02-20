#pragma once

#include "core.types.h"
#include "anim.track.fwd.h"
#include "math.curve.fwd.h"
#include "allocator.fwd.h"

#define MEL_ANIM_TRACK_F32  1
#define MEL_ANIM_TRACK_VEC2 2
#define MEL_ANIM_TRACK_VEC4 4

struct Mel_Anim_Track {
    u64 property_id;
    u32 stride;
    u32 keyframe_count;
    f32* times;
    f32* values;
    u32* curve_types;
    Mel_Bezier* beziers;
    Mel_Blend_Fn blend;
};

void mel_anim_track_init(Mel_Anim_Track* track, const Mel_Alloc* alloc,
                         u64 property_id, u32 stride, u32 keyframe_count);
void mel_anim_track_destroy(Mel_Anim_Track* track, const Mel_Alloc* alloc);

void mel_anim_track_set_keyframe(Mel_Anim_Track* track, u32 index,
                                 f32 time, const f32* value, u32 curve_type);
void mel_anim_track_set_bezier(Mel_Anim_Track* track, const Mel_Alloc* alloc,
                               u32 index, f32 cx1, f32 cy1, f32 cx2, f32 cy2);

void mel_anim_track_eval(const Mel_Anim_Track* track, f32 time, f32* out);
u32  mel_anim_track_find_keyframe(const Mel_Anim_Track* track, f32 time);

void mel_blend_angle(f32* out, const f32* a, const f32* b, u32 stride, f32 t);
void mel_blend_quat_slerp(f32* out, const f32* a, const f32* b, u32 stride, f32 t);
