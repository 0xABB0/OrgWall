#pragma once

#include <core/compiler.h>

#include "core.types.h"
#include "math.vec2.h"

typedef f64 f64x2 MEL_VECTOR_TYPE(2);

typedef union
{
    f64x2 v;
    struct { f64 x, y; };
    struct { f64 u, v_; };
    struct { f64 w, h; };
    f64 e[2];
} Mel_DVec2;

#define MEL_DVEC2_ZERO  ((Mel_DVec2){ .v = (f64x2){0, 0} })
#define MEL_DVEC2_ONE   ((Mel_DVec2){ .v = (f64x2){1, 1} })
#define MEL_DVEC2_UP    ((Mel_DVec2){ .v = (f64x2){0, 1} })
#define MEL_DVEC2_DOWN  ((Mel_DVec2){ .v = (f64x2){0, -1} })
#define MEL_DVEC2_LEFT  ((Mel_DVec2){ .v = (f64x2){-1, 0} })
#define MEL_DVEC2_RIGHT ((Mel_DVec2){ .v = (f64x2){1, 0} })

MEL_NODISCARD static inline Mel_DVec2 mel_dvec2(f64 x, f64 y);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_add(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_sub(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_mul(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_div(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_scale(Mel_DVec2 v, f64 s);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_negate(Mel_DVec2 v);
MEL_NODISCARD static inline f64 mel_dvec2_dot(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline f64 mel_dvec2_len_sq(Mel_DVec2 v);
MEL_NODISCARD static inline f64 mel_dvec2_len(Mel_DVec2 v);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_normalize(Mel_DVec2 v);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_lerp(Mel_DVec2 a, Mel_DVec2 b, f64 t);
MEL_NODISCARD static inline f64 mel_dvec2_dist_sq(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline f64 mel_dvec2_dist(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_min(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_max(Mel_DVec2 a, Mel_DVec2 b);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_abs(Mel_DVec2 v);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_floor(Mel_DVec2 v);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_ceil(Mel_DVec2 v);
MEL_NODISCARD static inline Mel_DVec2 mel_dvec2_round(Mel_DVec2 v);
MEL_NODISCARD static inline Mel_Vec2 mel_dvec2_to_vec2(Mel_DVec2 v);
MEL_NODISCARD static inline Mel_DVec2 mel_vec2_to_dvec2(Mel_Vec2 v);

#include "math.dvec2.inl"
