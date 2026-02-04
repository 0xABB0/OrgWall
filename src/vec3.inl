#ifdef _CLANGD
#pragma once
#include "vec3.h"
#endif

static inline Mel_Vec3 mel_vec3(f32 x, f32 y, f32 z)
{
    return (Mel_Vec3){ .v = (f32x3){x, y, z} };
}

static inline Mel_Vec3 mel_vec3_add(Mel_Vec3 a, Mel_Vec3 b)
{
    return (Mel_Vec3){ .v = a.v + b.v };
}

static inline Mel_Vec3 mel_vec3_sub(Mel_Vec3 a, Mel_Vec3 b)
{
    return (Mel_Vec3){ .v = a.v - b.v };
}

static inline Mel_Vec3 mel_vec3_mul(Mel_Vec3 a, Mel_Vec3 b)
{
    return (Mel_Vec3){ .v = a.v * b.v };
}

static inline Mel_Vec3 mel_vec3_div(Mel_Vec3 a, Mel_Vec3 b)
{
    return (Mel_Vec3){ .v = a.v / b.v };
}

static inline Mel_Vec3 mel_vec3_scale(Mel_Vec3 v, f32 s)
{
    return (Mel_Vec3){ .v = v.v * s };
}

static inline Mel_Vec3 mel_vec3_negate(Mel_Vec3 v)
{
    return (Mel_Vec3){ .v = -v.v };
}

static inline f32 mel_vec3_dot(Mel_Vec3 a, Mel_Vec3 b)
{
    f32x3 m = a.v * b.v;
    return m[0] + m[1] + m[2];
}

static inline Mel_Vec3 mel_vec3_cross(Mel_Vec3 a, Mel_Vec3 b)
{
    return (Mel_Vec3){ .v = (f32x3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    }};
}

static inline f32 mel_vec3_len_sq(Mel_Vec3 v)
{
    return mel_vec3_dot(v, v);
}

static inline f32 mel_vec3_len(Mel_Vec3 v)
{
    return __builtin_sqrtf(mel_vec3_len_sq(v));
}

static inline Mel_Vec3 mel_vec3_normalize(Mel_Vec3 v)
{
    f32 len = mel_vec3_len(v);
    assert(len > 0.0f);
    return mel_vec3_scale(v, 1.0f / len);
}

static inline Mel_Vec3 mel_vec3_lerp(Mel_Vec3 a, Mel_Vec3 b, f32 t)
{
    return (Mel_Vec3){ .v = a.v + (b.v - a.v) * t };
}

static inline f32 mel_vec3_dist_sq(Mel_Vec3 a, Mel_Vec3 b)
{
    return mel_vec3_len_sq(mel_vec3_sub(b, a));
}

static inline f32 mel_vec3_dist(Mel_Vec3 a, Mel_Vec3 b)
{
    return __builtin_sqrtf(mel_vec3_dist_sq(a, b));
}

static inline Mel_Vec3 mel_vec3_min(Mel_Vec3 a, Mel_Vec3 b)
{
    return (Mel_Vec3){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_Vec3 mel_vec3_max(Mel_Vec3 a, Mel_Vec3 b)
{
    return (Mel_Vec3){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_Vec3 mel_vec3_abs(Mel_Vec3 v)
{
    return (Mel_Vec3){ .v = __builtin_elementwise_abs(v.v) };
}

static inline Mel_Vec3 mel_vec3_floor(Mel_Vec3 v)
{
    return (Mel_Vec3){ .v = __builtin_elementwise_floor(v.v) };
}

static inline Mel_Vec3 mel_vec3_ceil(Mel_Vec3 v)
{
    return (Mel_Vec3){ .v = __builtin_elementwise_ceil(v.v) };
}

static inline Mel_Vec3 mel_vec3_round(Mel_Vec3 v)
{
    return (Mel_Vec3){ .v = __builtin_elementwise_round(v.v) };
}

static inline Mel_Vec3 mel_vec3_reflect(Mel_Vec3 v, Mel_Vec3 n)
{
    f32 d = 2.0f * mel_vec3_dot(v, n);
    return mel_vec3_sub(v, mel_vec3_scale(n, d));
}
