#ifdef _CLANGD
#pragma once
#include "math.dvec2.h"
#endif

static inline Mel_DVec2 mel_dvec2(f64 x, f64 y)
{
    return (Mel_DVec2){ .v = (f64x2){x, y} };
}

static inline Mel_DVec2 mel_dvec2_add(Mel_DVec2 a, Mel_DVec2 b)
{
    return (Mel_DVec2){ .v = a.v + b.v };
}

static inline Mel_DVec2 mel_dvec2_sub(Mel_DVec2 a, Mel_DVec2 b)
{
    return (Mel_DVec2){ .v = a.v - b.v };
}

static inline Mel_DVec2 mel_dvec2_mul(Mel_DVec2 a, Mel_DVec2 b)
{
    return (Mel_DVec2){ .v = a.v * b.v };
}

static inline Mel_DVec2 mel_dvec2_div(Mel_DVec2 a, Mel_DVec2 b)
{
    return (Mel_DVec2){ .v = a.v / b.v };
}

static inline Mel_DVec2 mel_dvec2_scale(Mel_DVec2 v, f64 s)
{
    return (Mel_DVec2){ .v = v.v * s };
}

static inline Mel_DVec2 mel_dvec2_negate(Mel_DVec2 v)
{
    return (Mel_DVec2){ .v = -v.v };
}

static inline f64 mel_dvec2_dot(Mel_DVec2 a, Mel_DVec2 b)
{
    f64x2 m = a.v * b.v;
    return m[0] + m[1];
}

static inline f64 mel_dvec2_len_sq(Mel_DVec2 v)
{
    return mel_dvec2_dot(v, v);
}

static inline f64 mel_dvec2_len(Mel_DVec2 v)
{
    return __builtin_sqrt(mel_dvec2_len_sq(v));
}

static inline Mel_DVec2 mel_dvec2_normalize(Mel_DVec2 v)
{
    f64 len = mel_dvec2_len(v);
    assert(len > 0.0);
    return mel_dvec2_scale(v, 1.0 / len);
}

static inline Mel_DVec2 mel_dvec2_lerp(Mel_DVec2 a, Mel_DVec2 b, f64 t)
{
    return (Mel_DVec2){ .v = a.v + (b.v - a.v) * t };
}

static inline f64 mel_dvec2_dist_sq(Mel_DVec2 a, Mel_DVec2 b)
{
    return mel_dvec2_len_sq(mel_dvec2_sub(b, a));
}

static inline f64 mel_dvec2_dist(Mel_DVec2 a, Mel_DVec2 b)
{
    return __builtin_sqrt(mel_dvec2_dist_sq(a, b));
}

static inline Mel_DVec2 mel_dvec2_min(Mel_DVec2 a, Mel_DVec2 b)
{
    return (Mel_DVec2){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_DVec2 mel_dvec2_max(Mel_DVec2 a, Mel_DVec2 b)
{
    return (Mel_DVec2){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_DVec2 mel_dvec2_abs(Mel_DVec2 v)
{
    return (Mel_DVec2){ .v = __builtin_elementwise_abs(v.v) };
}

static inline Mel_DVec2 mel_dvec2_floor(Mel_DVec2 v)
{
    return (Mel_DVec2){ .v = __builtin_elementwise_floor(v.v) };
}

static inline Mel_DVec2 mel_dvec2_ceil(Mel_DVec2 v)
{
    return (Mel_DVec2){ .v = __builtin_elementwise_ceil(v.v) };
}

static inline Mel_DVec2 mel_dvec2_round(Mel_DVec2 v)
{
    return (Mel_DVec2){ .v = __builtin_elementwise_round(v.v) };
}

static inline Mel_Vec2 mel_dvec2_to_vec2(Mel_DVec2 v)
{
    return (Mel_Vec2){ .v = (f32x2){(f32)v.x, (f32)v.y} };
}

static inline Mel_DVec2 mel_vec2_to_dvec2(Mel_Vec2 v)
{
    return (Mel_DVec2){ .v = (f64x2){(f64)v.x, (f64)v.y} };
}
