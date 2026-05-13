#pragma once

#include <core/compiler.h>

#include <core/types.h>
#include "vec4.h"

typedef f64 f64x4 MEL_VECTOR_TYPE(4);

typedef union
{
    f64x4 v;
    struct { f64 x, y, z, w; };
    struct { f64 r, g, b, a; };
    f64 e[4];
} Mel_DVec4;

#define MEL_DVEC4_ZERO ((Mel_DVec4){ .v = (f64x4){0, 0, 0, 0} })
#define MEL_DVEC4_ONE  ((Mel_DVec4){ .v = (f64x4){1, 1, 1, 1} })

MEL_NODISCARD static inline Mel_DVec4 mel_dvec4(f64 x, f64 y, f64 z, f64 w);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_add(Mel_DVec4 a, Mel_DVec4 b);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_sub(Mel_DVec4 a, Mel_DVec4 b);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_mul(Mel_DVec4 a, Mel_DVec4 b);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_div(Mel_DVec4 a, Mel_DVec4 b);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_scale(Mel_DVec4 v, f64 s);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_negate(Mel_DVec4 v);
MEL_NODISCARD static inline f64 mel_dvec4_dot(Mel_DVec4 a, Mel_DVec4 b);
MEL_NODISCARD static inline f64 mel_dvec4_len_sq(Mel_DVec4 v);
MEL_NODISCARD static inline f64 mel_dvec4_len(Mel_DVec4 v);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_normalize(Mel_DVec4 v);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_lerp(Mel_DVec4 a, Mel_DVec4 b, f64 t);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_min(Mel_DVec4 a, Mel_DVec4 b);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_max(Mel_DVec4 a, Mel_DVec4 b);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_abs(Mel_DVec4 v);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_floor(Mel_DVec4 v);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_ceil(Mel_DVec4 v);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_round(Mel_DVec4 v);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_clamp(Mel_DVec4 v, Mel_DVec4 lo, Mel_DVec4 hi);
MEL_NODISCARD static inline Mel_DVec4 mel_dvec4_saturate(Mel_DVec4 v);
MEL_NODISCARD static inline Mel_Vec4 mel_dvec4_to_vec4(Mel_DVec4 v);
MEL_NODISCARD static inline Mel_DVec4 mel_vec4_to_dvec4(Mel_Vec4 v);

#include "dvec4.inl"
