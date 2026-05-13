#ifdef _CLANGD
#pragma once
#include "dvec4.h"
#endif

static inline Mel_DVec4 mel_dvec4(f64 x, f64 y, f64 z, f64 w)
{
    return (Mel_DVec4){ .v = (f64x4){x, y, z, w} };
}

static inline Mel_DVec4 mel_dvec4_add(Mel_DVec4 a, Mel_DVec4 b)
{
    return (Mel_DVec4){ .v = a.v + b.v };
}

static inline Mel_DVec4 mel_dvec4_sub(Mel_DVec4 a, Mel_DVec4 b)
{
    return (Mel_DVec4){ .v = a.v - b.v };
}

static inline Mel_DVec4 mel_dvec4_mul(Mel_DVec4 a, Mel_DVec4 b)
{
    return (Mel_DVec4){ .v = a.v * b.v };
}

static inline Mel_DVec4 mel_dvec4_div(Mel_DVec4 a, Mel_DVec4 b)
{
    return (Mel_DVec4){ .v = a.v / b.v };
}

static inline Mel_DVec4 mel_dvec4_scale(Mel_DVec4 v, f64 s)
{
    return (Mel_DVec4){ .v = v.v * s };
}

static inline Mel_DVec4 mel_dvec4_negate(Mel_DVec4 v)
{
    return (Mel_DVec4){ .v = -v.v };
}

static inline f64 mel_dvec4_dot(Mel_DVec4 a, Mel_DVec4 b)
{
    f64x4 m = a.v * b.v;
    return m[0] + m[1] + m[2] + m[3];
}

static inline f64 mel_dvec4_len_sq(Mel_DVec4 v)
{
    return mel_dvec4_dot(v, v);
}

static inline f64 mel_dvec4_len(Mel_DVec4 v)
{
    return __builtin_sqrt(mel_dvec4_len_sq(v));
}

static inline Mel_DVec4 mel_dvec4_normalize(Mel_DVec4 v)
{
    f64 len = mel_dvec4_len(v);
    assert(len > 0.0);
    return mel_dvec4_scale(v, 1.0 / len);
}

static inline Mel_DVec4 mel_dvec4_lerp(Mel_DVec4 a, Mel_DVec4 b, f64 t)
{
    return (Mel_DVec4){ .v = a.v + (b.v - a.v) * t };
}

static inline Mel_DVec4 mel_dvec4_min(Mel_DVec4 a, Mel_DVec4 b)
{
    return (Mel_DVec4){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_DVec4 mel_dvec4_max(Mel_DVec4 a, Mel_DVec4 b)
{
    return (Mel_DVec4){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_DVec4 mel_dvec4_abs(Mel_DVec4 v)
{
    return (Mel_DVec4){ .v = __builtin_elementwise_abs(v.v) };
}

static inline Mel_DVec4 mel_dvec4_floor(Mel_DVec4 v)
{
    return (Mel_DVec4){ .v = __builtin_elementwise_floor(v.v) };
}

static inline Mel_DVec4 mel_dvec4_ceil(Mel_DVec4 v)
{
    return (Mel_DVec4){ .v = __builtin_elementwise_ceil(v.v) };
}

static inline Mel_DVec4 mel_dvec4_round(Mel_DVec4 v)
{
    return (Mel_DVec4){ .v = __builtin_elementwise_round(v.v) };
}

static inline Mel_DVec4 mel_dvec4_clamp(Mel_DVec4 v, Mel_DVec4 lo, Mel_DVec4 hi)
{
    return mel_dvec4_min(mel_dvec4_max(v, lo), hi);
}

static inline Mel_DVec4 mel_dvec4_saturate(Mel_DVec4 v)
{
    return mel_dvec4_clamp(v, MEL_DVEC4_ZERO, MEL_DVEC4_ONE);
}

static inline Mel_Vec4 mel_dvec4_to_vec4(Mel_DVec4 v)
{
    return (Mel_Vec4){ .v = (f32x4){(f32)v.x, (f32)v.y, (f32)v.z, (f32)v.w} };
}

static inline Mel_DVec4 mel_vec4_to_dvec4(Mel_Vec4 v)
{
    return (Mel_DVec4){ .v = (f64x4){(f64)v.x, (f64)v.y, (f64)v.z, (f64)v.w} };
}
