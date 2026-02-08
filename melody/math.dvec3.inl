#ifdef _CLANGD
#pragma once
#include "math.dvec3.h"
#endif

static inline Mel_DVec3 mel_dvec3(f64 x, f64 y, f64 z)
{
    return (Mel_DVec3){ .v = (f64x3){x, y, z} };
}

static inline Mel_DVec3 mel_dvec3_add(Mel_DVec3 a, Mel_DVec3 b)
{
    return (Mel_DVec3){ .v = a.v + b.v };
}

static inline Mel_DVec3 mel_dvec3_sub(Mel_DVec3 a, Mel_DVec3 b)
{
    return (Mel_DVec3){ .v = a.v - b.v };
}

static inline Mel_DVec3 mel_dvec3_mul(Mel_DVec3 a, Mel_DVec3 b)
{
    return (Mel_DVec3){ .v = a.v * b.v };
}

static inline Mel_DVec3 mel_dvec3_div(Mel_DVec3 a, Mel_DVec3 b)
{
    return (Mel_DVec3){ .v = a.v / b.v };
}

static inline Mel_DVec3 mel_dvec3_scale(Mel_DVec3 v, f64 s)
{
    return (Mel_DVec3){ .v = v.v * s };
}

static inline Mel_DVec3 mel_dvec3_negate(Mel_DVec3 v)
{
    return (Mel_DVec3){ .v = -v.v };
}

static inline f64 mel_dvec3_dot(Mel_DVec3 a, Mel_DVec3 b)
{
    f64x3 m = a.v * b.v;
    return m[0] + m[1] + m[2];
}

static inline Mel_DVec3 mel_dvec3_cross(Mel_DVec3 a, Mel_DVec3 b)
{
    return (Mel_DVec3){ .v = (f64x3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    }};
}

static inline f64 mel_dvec3_len_sq(Mel_DVec3 v)
{
    return mel_dvec3_dot(v, v);
}

static inline f64 mel_dvec3_len(Mel_DVec3 v)
{
    return __builtin_sqrt(mel_dvec3_len_sq(v));
}

static inline Mel_DVec3 mel_dvec3_normalize(Mel_DVec3 v)
{
    f64 len = mel_dvec3_len(v);
    assert(len > 0.0);
    return mel_dvec3_scale(v, 1.0 / len);
}

static inline Mel_DVec3 mel_dvec3_lerp(Mel_DVec3 a, Mel_DVec3 b, f64 t)
{
    return (Mel_DVec3){ .v = a.v + (b.v - a.v) * t };
}

static inline f64 mel_dvec3_dist_sq(Mel_DVec3 a, Mel_DVec3 b)
{
    return mel_dvec3_len_sq(mel_dvec3_sub(b, a));
}

static inline f64 mel_dvec3_dist(Mel_DVec3 a, Mel_DVec3 b)
{
    return __builtin_sqrt(mel_dvec3_dist_sq(a, b));
}

static inline Mel_DVec3 mel_dvec3_min(Mel_DVec3 a, Mel_DVec3 b)
{
    return (Mel_DVec3){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_DVec3 mel_dvec3_max(Mel_DVec3 a, Mel_DVec3 b)
{
    return (Mel_DVec3){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_DVec3 mel_dvec3_abs(Mel_DVec3 v)
{
    return (Mel_DVec3){ .v = __builtin_elementwise_abs(v.v) };
}

static inline Mel_DVec3 mel_dvec3_floor(Mel_DVec3 v)
{
    return (Mel_DVec3){ .v = __builtin_elementwise_floor(v.v) };
}

static inline Mel_DVec3 mel_dvec3_ceil(Mel_DVec3 v)
{
    return (Mel_DVec3){ .v = __builtin_elementwise_ceil(v.v) };
}

static inline Mel_DVec3 mel_dvec3_round(Mel_DVec3 v)
{
    return (Mel_DVec3){ .v = __builtin_elementwise_round(v.v) };
}

static inline Mel_DVec3 mel_dvec3_reflect(Mel_DVec3 v, Mel_DVec3 n)
{
    f64 d = 2.0 * mel_dvec3_dot(v, n);
    return mel_dvec3_sub(v, mel_dvec3_scale(n, d));
}

static inline Mel_Vec3 mel_dvec3_to_vec3(Mel_DVec3 v)
{
    return (Mel_Vec3){ .v = (f32x3){(f32)v.x, (f32)v.y, (f32)v.z} };
}

static inline Mel_DVec3 mel_vec3_to_dvec3(Mel_Vec3 v)
{
    return (Mel_DVec3){ .v = (f64x3){(f64)v.x, (f64)v.y, (f64)v.z} };
}
