#ifdef _CLANGD
#pragma once
#include "math.ivec4.h"
#endif

static inline Mel_IVec4 mel_ivec4(i32 x, i32 y, i32 z, i32 w)
{
    return (Mel_IVec4){ .v = (i32x4){x, y, z, w} };
}

static inline Mel_IVec4 mel_ivec4_add(Mel_IVec4 a, Mel_IVec4 b)
{
    return (Mel_IVec4){ .v = a.v + b.v };
}

static inline Mel_IVec4 mel_ivec4_sub(Mel_IVec4 a, Mel_IVec4 b)
{
    return (Mel_IVec4){ .v = a.v - b.v };
}

static inline Mel_IVec4 mel_ivec4_mul(Mel_IVec4 a, Mel_IVec4 b)
{
    return (Mel_IVec4){ .v = a.v * b.v };
}

static inline Mel_IVec4 mel_ivec4_div(Mel_IVec4 a, Mel_IVec4 b)
{
    return (Mel_IVec4){ .v = a.v / b.v };
}

static inline Mel_IVec4 mel_ivec4_scale(Mel_IVec4 v, i32 s)
{
    return (Mel_IVec4){ .v = v.v * s };
}

static inline Mel_IVec4 mel_ivec4_negate(Mel_IVec4 v)
{
    return (Mel_IVec4){ .v = -v.v };
}

static inline Mel_IVec4 mel_ivec4_min(Mel_IVec4 a, Mel_IVec4 b)
{
    return (Mel_IVec4){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_IVec4 mel_ivec4_max(Mel_IVec4 a, Mel_IVec4 b)
{
    return (Mel_IVec4){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_IVec4 mel_ivec4_abs(Mel_IVec4 v)
{
    return (Mel_IVec4){ .v = __builtin_elementwise_abs(v.v) };
}

static inline i32 mel_ivec4_dot(Mel_IVec4 a, Mel_IVec4 b)
{
    i32x4 m = a.v * b.v;
    return m[0] + m[1] + m[2] + m[3];
}

static inline bool mel_ivec4_eq(Mel_IVec4 a, Mel_IVec4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

static inline Mel_Vec4 mel_ivec4_to_vec4(Mel_IVec4 v)
{
    return mel_vec4((f32)v.x, (f32)v.y, (f32)v.z, (f32)v.w);
}

static inline Mel_IVec4 mel_vec4_to_ivec4(Mel_Vec4 v)
{
    return mel_ivec4((i32)v.x, (i32)v.y, (i32)v.z, (i32)v.w);
}
