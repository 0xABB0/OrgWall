#pragma once

#include <core/compiler.h>

#include "core.types.h"
#include "math.vec3.h"

typedef f64 f64x3 MEL_VECTOR_TYPE(3);

typedef union
{
    f64x3 v;
    struct { f64 x, y, z; };
    struct { f64 r, g, b; };
    f64 e[3];
} Mel_DVec3;

#define MEL_DVEC3_ZERO    ((Mel_DVec3){ .v = (f64x3){0, 0, 0} })
#define MEL_DVEC3_ONE     ((Mel_DVec3){ .v = (f64x3){1, 1, 1} })
#define MEL_DVEC3_UP      ((Mel_DVec3){ .v = (f64x3){0, 1, 0} })
#define MEL_DVEC3_DOWN    ((Mel_DVec3){ .v = (f64x3){0, -1, 0} })
#define MEL_DVEC3_LEFT    ((Mel_DVec3){ .v = (f64x3){-1, 0, 0} })
#define MEL_DVEC3_RIGHT   ((Mel_DVec3){ .v = (f64x3){1, 0, 0} })
#define MEL_DVEC3_FORWARD ((Mel_DVec3){ .v = (f64x3){0, 0, -1} })
#define MEL_DVEC3_BACK    ((Mel_DVec3){ .v = (f64x3){0, 0, 1} })

MEL_NODISCARD static inline Mel_DVec3 mel_dvec3(f64 x, f64 y, f64 z);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_add(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_sub(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_mul(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_div(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_scale(Mel_DVec3 v, f64 s);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_negate(Mel_DVec3 v);
MEL_NODISCARD static inline f64 mel_dvec3_dot(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_cross(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline f64 mel_dvec3_len_sq(Mel_DVec3 v);
MEL_NODISCARD static inline f64 mel_dvec3_len(Mel_DVec3 v);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_normalize(Mel_DVec3 v);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_lerp(Mel_DVec3 a, Mel_DVec3 b, f64 t);
MEL_NODISCARD static inline f64 mel_dvec3_dist_sq(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline f64 mel_dvec3_dist(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_min(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_max(Mel_DVec3 a, Mel_DVec3 b);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_abs(Mel_DVec3 v);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_floor(Mel_DVec3 v);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_ceil(Mel_DVec3 v);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_round(Mel_DVec3 v);
MEL_NODISCARD static inline Mel_DVec3 mel_dvec3_reflect(Mel_DVec3 v, Mel_DVec3 n);
MEL_NODISCARD static inline Mel_Vec3 mel_dvec3_to_vec3(Mel_DVec3 v);
MEL_NODISCARD static inline Mel_DVec3 mel_vec3_to_dvec3(Mel_Vec3 v);

#include "math.dvec3.inl"
