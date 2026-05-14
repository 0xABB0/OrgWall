#ifdef _CLANGD
#pragma once
#include "ivec2.h"
#endif

static inline Mel_IVec2 mel_ivec2(i32 x, i32 y)
{
    return (Mel_IVec2){ .v = (i32x2){x, y} };
}

static inline Mel_IVec2 mel_ivec2_add(Mel_IVec2 a, Mel_IVec2 b)
{
    return (Mel_IVec2){ .v = a.v + b.v };
}

static inline Mel_IVec2 mel_ivec2_sub(Mel_IVec2 a, Mel_IVec2 b)
{
    return (Mel_IVec2){ .v = a.v - b.v };
}

static inline Mel_IVec2 mel_ivec2_mul(Mel_IVec2 a, Mel_IVec2 b)
{
    return (Mel_IVec2){ .v = a.v * b.v };
}

static inline Mel_IVec2 mel_ivec2_div(Mel_IVec2 a, Mel_IVec2 b)
{
    return (Mel_IVec2){ .v = a.v / b.v };
}

static inline Mel_IVec2 mel_ivec2_scale(Mel_IVec2 v, i32 s)
{
    return (Mel_IVec2){ .v = v.v * s };
}

static inline Mel_IVec2 mel_ivec2_negate(Mel_IVec2 v)
{
    return (Mel_IVec2){ .v = -v.v };
}

static inline Mel_IVec2 mel_ivec2_min(Mel_IVec2 a, Mel_IVec2 b)
{
    return (Mel_IVec2){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_IVec2 mel_ivec2_max(Mel_IVec2 a, Mel_IVec2 b)
{
    return (Mel_IVec2){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_IVec2 mel_ivec2_abs(Mel_IVec2 v)
{
    return (Mel_IVec2){ .v = __builtin_elementwise_abs(v.v) };
}

static inline i32 mel_ivec2_dot(Mel_IVec2 a, Mel_IVec2 b)
{
    i32x2 m = a.v * b.v;
    return m[0] + m[1];
}

static inline bool mel_ivec2_eq(Mel_IVec2 a, Mel_IVec2 b)
{
    return a.x == b.x && a.y == b.y;
}

static inline Mel_Vec2 mel_ivec2_to_vec2(Mel_IVec2 v)
{
    return mel_vec2((f32)v.x, (f32)v.y);
}

static inline Mel_IVec2 mel_vec2_to_ivec2(Mel_Vec2 v)
{
    return mel_ivec2((i32)v.x, (i32)v.y);
}
