#pragma once

#include "core.types.h"
#include "math.vec4.h"

typedef f64 f64x4 __attribute__((ext_vector_type(4)));

typedef union
{
    f64x4 v;
    struct { f64 x, y, z, w; };
    struct { f64 r, g, b, a; };
    f64 e[4];
} Mel_DVec4;

#define MEL_DVEC4_ZERO ((Mel_DVec4){ .v = (f64x4){0, 0, 0, 0} })
#define MEL_DVEC4_ONE  ((Mel_DVec4){ .v = (f64x4){1, 1, 1, 1} })

[[nodiscard]] static inline Mel_DVec4 mel_dvec4(f64 x, f64 y, f64 z, f64 w);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_add(Mel_DVec4 a, Mel_DVec4 b);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_sub(Mel_DVec4 a, Mel_DVec4 b);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_mul(Mel_DVec4 a, Mel_DVec4 b);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_div(Mel_DVec4 a, Mel_DVec4 b);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_scale(Mel_DVec4 v, f64 s);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_negate(Mel_DVec4 v);
[[nodiscard]] static inline f64 mel_dvec4_dot(Mel_DVec4 a, Mel_DVec4 b);
[[nodiscard]] static inline f64 mel_dvec4_len_sq(Mel_DVec4 v);
[[nodiscard]] static inline f64 mel_dvec4_len(Mel_DVec4 v);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_normalize(Mel_DVec4 v);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_lerp(Mel_DVec4 a, Mel_DVec4 b, f64 t);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_min(Mel_DVec4 a, Mel_DVec4 b);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_max(Mel_DVec4 a, Mel_DVec4 b);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_abs(Mel_DVec4 v);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_floor(Mel_DVec4 v);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_ceil(Mel_DVec4 v);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_round(Mel_DVec4 v);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_clamp(Mel_DVec4 v, Mel_DVec4 lo, Mel_DVec4 hi);
[[nodiscard]] static inline Mel_DVec4 mel_dvec4_saturate(Mel_DVec4 v);
[[nodiscard]] static inline Mel_Vec4 mel_dvec4_to_vec4(Mel_DVec4 v);
[[nodiscard]] static inline Mel_DVec4 mel_vec4_to_dvec4(Mel_Vec4 v);

#include "math.dvec4.inl"
