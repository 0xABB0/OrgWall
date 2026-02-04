#ifdef _CLANGD
#pragma once
#include "vec2.h"
#endif

static inline Mel_Vec2 mel_vec2(f32 x, f32 y)
{
    return (Mel_Vec2){ .v = (f32x2){x, y} };
}

static inline Mel_Vec2 mel_vec2_add(Mel_Vec2 a, Mel_Vec2 b)
{
    return (Mel_Vec2){ .v = a.v + b.v };
}

static inline Mel_Vec2 mel_vec2_sub(Mel_Vec2 a, Mel_Vec2 b)
{
    return (Mel_Vec2){ .v = a.v - b.v };
}

static inline Mel_Vec2 mel_vec2_mul(Mel_Vec2 a, Mel_Vec2 b)
{
    return (Mel_Vec2){ .v = a.v * b.v };
}

static inline Mel_Vec2 mel_vec2_div(Mel_Vec2 a, Mel_Vec2 b)
{
    return (Mel_Vec2){ .v = a.v / b.v };
}

static inline Mel_Vec2 mel_vec2_scale(Mel_Vec2 v, f32 s)
{
    return (Mel_Vec2){ .v = v.v * s };
}

static inline Mel_Vec2 mel_vec2_negate(Mel_Vec2 v)
{
    return (Mel_Vec2){ .v = -v.v };
}

static inline f32 mel_vec2_dot(Mel_Vec2 a, Mel_Vec2 b)
{
    f32x2 m = a.v * b.v;
    return m[0] + m[1];
}

static inline f32 mel_vec2_len_sq(Mel_Vec2 v)
{
    return mel_vec2_dot(v, v);
}

static inline f32 mel_vec2_len(Mel_Vec2 v)
{
    return __builtin_sqrtf(mel_vec2_len_sq(v));
}

static inline Mel_Vec2 mel_vec2_normalize(Mel_Vec2 v)
{
    f32 len = mel_vec2_len(v);
    assert(len > 0.0f);
    return mel_vec2_scale(v, 1.0f / len);
}

static inline Mel_Vec2 mel_vec2_lerp(Mel_Vec2 a, Mel_Vec2 b, f32 t)
{
    return (Mel_Vec2){ .v = a.v + (b.v - a.v) * t };
}

static inline f32 mel_vec2_dist_sq(Mel_Vec2 a, Mel_Vec2 b)
{
    return mel_vec2_len_sq(mel_vec2_sub(b, a));
}

static inline f32 mel_vec2_dist(Mel_Vec2 a, Mel_Vec2 b)
{
    return __builtin_sqrtf(mel_vec2_dist_sq(a, b));
}

static inline Mel_Vec2 mel_vec2_min(Mel_Vec2 a, Mel_Vec2 b)
{
    return (Mel_Vec2){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_Vec2 mel_vec2_max(Mel_Vec2 a, Mel_Vec2 b)
{
    return (Mel_Vec2){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_Vec2 mel_vec2_abs(Mel_Vec2 v)
{
    return (Mel_Vec2){ .v = __builtin_elementwise_abs(v.v) };
}

static inline Mel_Vec2 mel_vec2_floor(Mel_Vec2 v)
{
    return (Mel_Vec2){ .v = __builtin_elementwise_floor(v.v) };
}

static inline Mel_Vec2 mel_vec2_ceil(Mel_Vec2 v)
{
    return (Mel_Vec2){ .v = __builtin_elementwise_ceil(v.v) };
}

static inline Mel_Vec2 mel_vec2_round(Mel_Vec2 v)
{
    return (Mel_Vec2){ .v = __builtin_elementwise_round(v.v) };
}
