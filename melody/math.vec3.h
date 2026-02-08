#pragma once

#include "types.h"
#include "math.vec2.h"
#include <math.h>

typedef union
{
    f32x3 v;
    struct { f32 x, y, z; };
    struct { f32 r, g, b; };
    f32 e[3];
} Mel_Vec3;

#define MEL_VEC3_ZERO    ((Mel_Vec3){ .v = (f32x3){0, 0, 0} })
#define MEL_VEC3_ONE     ((Mel_Vec3){ .v = (f32x3){1, 1, 1} })
#define MEL_VEC3_UP      ((Mel_Vec3){ .v = (f32x3){0, 1, 0} })
#define MEL_VEC3_DOWN    ((Mel_Vec3){ .v = (f32x3){0, -1, 0} })
#define MEL_VEC3_LEFT    ((Mel_Vec3){ .v = (f32x3){-1, 0, 0} })
#define MEL_VEC3_RIGHT   ((Mel_Vec3){ .v = (f32x3){1, 0, 0} })
#define MEL_VEC3_FORWARD ((Mel_Vec3){ .v = (f32x3){0, 0, -1} })
#define MEL_VEC3_BACK    ((Mel_Vec3){ .v = (f32x3){0, 0, 1} })

[[nodiscard]] static inline Mel_Vec3 mel_vec3(f32 x, f32 y, f32 z);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_add(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_sub(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_mul(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_div(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_scale(Mel_Vec3 v, f32 s);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_negate(Mel_Vec3 v);
[[nodiscard]] static inline f32 mel_vec3_dot(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_cross(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline f32 mel_vec3_len_sq(Mel_Vec3 v);
[[nodiscard]] static inline f32 mel_vec3_len(Mel_Vec3 v);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_normalize(Mel_Vec3 v);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_lerp(Mel_Vec3 a, Mel_Vec3 b, f32 t);
[[nodiscard]] static inline f32 mel_vec3_dist_sq(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline f32 mel_vec3_dist(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_min(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_max(Mel_Vec3 a, Mel_Vec3 b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_abs(Mel_Vec3 v);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_floor(Mel_Vec3 v);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_ceil(Mel_Vec3 v);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_round(Mel_Vec3 v);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_reflect(Mel_Vec3 v, Mel_Vec3 n);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_rcp(Mel_Vec3 v);
static inline void mel_vec3_tangent(Mel_Vec3 normal, Mel_Vec3* t, Mel_Vec3* b);
static inline void mel_vec3_tangent_angle(Mel_Vec3 normal, f32 angle, Mel_Vec3* t, Mel_Vec3* b);
[[nodiscard]] static inline Mel_Vec3 mel_vec3_fromlatlong(f32 u, f32 v);
[[nodiscard]] static inline Mel_Vec2 mel_vec3_tolatlong(Mel_Vec3 dir);

#include "math.vec3.inl"
