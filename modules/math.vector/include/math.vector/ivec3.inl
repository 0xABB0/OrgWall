#ifdef _CLANGD
#pragma once
#include "ivec3.h"
#endif

static inline Mel_IVec3 mel_ivec3(i32 x, i32 y, i32 z)
{
    return (Mel_IVec3){ .v = (i32x3){x, y, z} };
}

static inline Mel_IVec3 mel_ivec3_add(Mel_IVec3 a, Mel_IVec3 b)
{
    return (Mel_IVec3){ .v = a.v + b.v };
}

static inline Mel_IVec3 mel_ivec3_sub(Mel_IVec3 a, Mel_IVec3 b)
{
    return (Mel_IVec3){ .v = a.v - b.v };
}

static inline Mel_IVec3 mel_ivec3_mul(Mel_IVec3 a, Mel_IVec3 b)
{
    return (Mel_IVec3){ .v = a.v * b.v };
}

static inline Mel_IVec3 mel_ivec3_div(Mel_IVec3 a, Mel_IVec3 b)
{
    return (Mel_IVec3){ .v = a.v / b.v };
}

static inline Mel_IVec3 mel_ivec3_scale(Mel_IVec3 v, i32 s)
{
    return (Mel_IVec3){ .v = v.v * s };
}

static inline Mel_IVec3 mel_ivec3_negate(Mel_IVec3 v)
{
    return (Mel_IVec3){ .v = -v.v };
}

static inline Mel_IVec3 mel_ivec3_min(Mel_IVec3 a, Mel_IVec3 b)
{
    return (Mel_IVec3){ .v = __builtin_elementwise_min(a.v, b.v) };
}

static inline Mel_IVec3 mel_ivec3_max(Mel_IVec3 a, Mel_IVec3 b)
{
    return (Mel_IVec3){ .v = __builtin_elementwise_max(a.v, b.v) };
}

static inline Mel_IVec3 mel_ivec3_abs(Mel_IVec3 v)
{
    return (Mel_IVec3){ .v = __builtin_elementwise_abs(v.v) };
}

static inline i32 mel_ivec3_dot(Mel_IVec3 a, Mel_IVec3 b)
{
    i32x3 m = a.v * b.v;
    return m[0] + m[1] + m[2];
}

static inline bool mel_ivec3_eq(Mel_IVec3 a, Mel_IVec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static inline Mel_Vec3 mel_ivec3_to_vec3(Mel_IVec3 v)
{
    return mel_vec3((f32)v.x, (f32)v.y, (f32)v.z);
}

static inline Mel_IVec3 mel_vec3_to_ivec3(Mel_Vec3 v)
{
    return mel_ivec3((i32)v.x, (i32)v.y, (i32)v.z);
}
