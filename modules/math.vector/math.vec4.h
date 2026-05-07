#pragma once

#include "core.types.h"
#include <math.h>

typedef f32 f32x4 __attribute__((ext_vector_type(4)));

typedef union
{
    f32x4 v;
    struct { f32 x, y, z, w; };
    struct { f32 r, g, b, a; };
    f32 e[4];
} Mel_Vec4;

#define MEL_VEC4_ZERO ((Mel_Vec4){ .v = (f32x4){0, 0, 0, 0} })
#define MEL_VEC4_ONE  ((Mel_Vec4){ .v = (f32x4){1, 1, 1, 1} })
#define MEL_VEC4(x, y, z, w) ((Mel_Vec4){ .v = (f32x4){(x), (y), (z), (w)} })

[[nodiscard]] static inline Mel_Vec4 mel_vec4(f32 x, f32 y, f32 z, f32 w);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_add(Mel_Vec4 a, Mel_Vec4 b);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_sub(Mel_Vec4 a, Mel_Vec4 b);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_mul(Mel_Vec4 a, Mel_Vec4 b);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_div(Mel_Vec4 a, Mel_Vec4 b);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_scale(Mel_Vec4 v, f32 s);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_negate(Mel_Vec4 v);
[[nodiscard]] static inline f32 mel_vec4_dot(Mel_Vec4 a, Mel_Vec4 b);
[[nodiscard]] static inline f32 mel_vec4_len_sq(Mel_Vec4 v);
[[nodiscard]] static inline f32 mel_vec4_len(Mel_Vec4 v);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_normalize(Mel_Vec4 v);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_lerp(Mel_Vec4 a, Mel_Vec4 b, f32 t);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_min(Mel_Vec4 a, Mel_Vec4 b);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_max(Mel_Vec4 a, Mel_Vec4 b);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_abs(Mel_Vec4 v);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_floor(Mel_Vec4 v);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_ceil(Mel_Vec4 v);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_round(Mel_Vec4 v);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_clamp(Mel_Vec4 v, Mel_Vec4 lo, Mel_Vec4 hi);
[[nodiscard]] static inline Mel_Vec4 mel_vec4_saturate(Mel_Vec4 v);

#include "math.vec4.inl"
