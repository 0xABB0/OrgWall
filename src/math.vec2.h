#pragma once

#include "types.h"
#include <math.h>

typedef union
{
    f32x2 v;
    struct { f32 x, y; };
    struct { f32 u, v_; };
    struct { f32 w, h; };
    f32 e[2];
} Mel_Vec2;

#define MEL_VEC2_ZERO  ((Mel_Vec2){ .v = (f32x2){0, 0} })
#define MEL_VEC2_ONE   ((Mel_Vec2){ .v = (f32x2){1, 1} })
#define MEL_VEC2_UP    ((Mel_Vec2){ .v = (f32x2){0, 1} })
#define MEL_VEC2_DOWN  ((Mel_Vec2){ .v = (f32x2){0, -1} })
#define MEL_VEC2_LEFT  ((Mel_Vec2){ .v = (f32x2){-1, 0} })
#define MEL_VEC2_RIGHT ((Mel_Vec2){ .v = (f32x2){1, 0} })

[[nodiscard]] static inline Mel_Vec2 mel_vec2(f32 x, f32 y);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_add(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_sub(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_mul(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_div(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_scale(Mel_Vec2 v, f32 s);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_negate(Mel_Vec2 v);
[[nodiscard]] static inline f32 mel_vec2_dot(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline f32 mel_vec2_len_sq(Mel_Vec2 v);
[[nodiscard]] static inline f32 mel_vec2_len(Mel_Vec2 v);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_normalize(Mel_Vec2 v);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_lerp(Mel_Vec2 a, Mel_Vec2 b, f32 t);
[[nodiscard]] static inline f32 mel_vec2_dist_sq(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline f32 mel_vec2_dist(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_min(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_max(Mel_Vec2 a, Mel_Vec2 b);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_abs(Mel_Vec2 v);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_floor(Mel_Vec2 v);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_ceil(Mel_Vec2 v);
[[nodiscard]] static inline Mel_Vec2 mel_vec2_round(Mel_Vec2 v);

#include "math.vec2.inl"
