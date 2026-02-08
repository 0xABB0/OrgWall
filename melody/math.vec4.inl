#ifdef _CLANGD
#pragma once
#include "math.vec4.h"
#endif

static inline Mel_Vec4 mel_vec4(f32 x, f32 y, f32 z, f32 w)
{
    return (Mel_Vec4){ .v = (f32x4){x, y, z, w} };
}

static inline Mel_Vec4 mel_vec4_add(Mel_Vec4 a, Mel_Vec4 b)
{
    return (Mel_Vec4){ .v = a.v + b.v };
}

static inline Mel_Vec4 mel_vec4_sub(Mel_Vec4 a, Mel_Vec4 b)
{
    return (Mel_Vec4){ .v = a.v - b.v };
}

static inline Mel_Vec4 mel_vec4_mul(Mel_Vec4 a, Mel_Vec4 b)
{
    return (Mel_Vec4){ .v = a.v * b.v };
}

static inline Mel_Vec4 mel_vec4_div(Mel_Vec4 a, Mel_Vec4 b)
{
    return (Mel_Vec4){ .v = a.v / b.v };
}

static inline Mel_Vec4 mel_vec4_scale(Mel_Vec4 v, f32 s)
{
    return (Mel_Vec4){ .v = v.v * s };
}

static inline Mel_Vec4 mel_vec4_negate(Mel_Vec4 v)
{
    return (Mel_Vec4){ .v = -v.v };
}

static inline f32 mel_vec4_dot(Mel_Vec4 a, Mel_Vec4 b)
{
    f32x4 m = a.v * b.v;
    return m[0] + m[1] + m[2] + m[3];
}

static inline f32 mel_vec4_len_sq(Mel_Vec4 v)
{
    return mel_vec4_dot(v, v);
}

static inline f32 mel_vec4_len(Mel_Vec4 v)
{
    return __builtin_sqrtf(mel_vec4_len_sq(v));
}

static inline Mel_Vec4 mel_vec4_normalize(Mel_Vec4 v)
{
    f32 len = mel_vec4_len(v);
    assert(len > 0.0f);
    return mel_vec4_scale(v, 1.0f / len);
}

static inline Mel_Vec4 mel_vec4_lerp(Mel_Vec4 a, Mel_Vec4 b, f32 t)
{
    return (Mel_Vec4){ .v = a.v + (b.v - a.v) * t };
}

static inline Mel_Vec4 mel_vec4_min(Mel_Vec4 a, Mel_Vec4 b)
{
    return (Mel_Vec4){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_Vec4 mel_vec4_max(Mel_Vec4 a, Mel_Vec4 b)
{
    return (Mel_Vec4){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_Vec4 mel_vec4_abs(Mel_Vec4 v)
{
    return (Mel_Vec4){ .v = __builtin_elementwise_abs(v.v) };
}

static inline Mel_Vec4 mel_vec4_floor(Mel_Vec4 v)
{
    return (Mel_Vec4){ .v = __builtin_elementwise_floor(v.v) };
}

static inline Mel_Vec4 mel_vec4_ceil(Mel_Vec4 v)
{
    return (Mel_Vec4){ .v = __builtin_elementwise_ceil(v.v) };
}

static inline Mel_Vec4 mel_vec4_round(Mel_Vec4 v)
{
    return (Mel_Vec4){ .v = __builtin_elementwise_round(v.v) };
}

static inline Mel_Vec4 mel_vec4_clamp(Mel_Vec4 v, Mel_Vec4 lo, Mel_Vec4 hi)
{
    return mel_vec4_min(mel_vec4_max(v, lo), hi);
}

static inline Mel_Vec4 mel_vec4_saturate(Mel_Vec4 v)
{
    return mel_vec4_clamp(v, MEL_VEC4_ZERO, MEL_VEC4_ONE);
}
