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

static inline Mel_Vec3 mel_vec3_rcp(Mel_Vec3 v)
{
    return (Mel_Vec3){ .v = (f32x3){1.0f / v.x, 1.0f / v.y, 1.0f / v.z} };
}

static inline void mel_vec3_tangent(Mel_Vec3 normal, Mel_Vec3* t, Mel_Vec3* b)
{
    Mel_Vec3 up = __builtin_fabsf(normal.y) < 0.999f
        ? mel_vec3(0.0f, 1.0f, 0.0f)
        : mel_vec3(1.0f, 0.0f, 0.0f);
    *t = mel_vec3_normalize(mel_vec3_cross(up, normal));
    *b = mel_vec3_cross(normal, *t);
}

static inline void mel_vec3_tangent_angle(Mel_Vec3 normal, f32 angle, Mel_Vec3* t, Mel_Vec3* b)
{
    mel_vec3_tangent(normal, t, b);
    f32 c = __builtin_cosf(angle);
    f32 s = __builtin_sinf(angle);
    Mel_Vec3 t0 = *t;
    *t = mel_vec3_add(mel_vec3_scale(t0, c), mel_vec3_scale(*b, s));
    *b = mel_vec3_cross(normal, *t);
}

static inline Mel_Vec3 mel_vec3_fromlatlong(f32 u, f32 v)
{
    f32 phi = u * 2.0f * 3.14159265358979323846f;
    f32 theta = v * 3.14159265358979323846f;
    f32 st = __builtin_sinf(theta);
    return mel_vec3(
        -st * __builtin_sinf(phi),
        __builtin_cosf(theta),
        -st * __builtin_cosf(phi)
    );
}

static inline Mel_Vec2 mel_vec3_tolatlong(Mel_Vec3 dir)
{
    f32 phi = __builtin_atan2f(-dir.x, -dir.z);
    f32 theta = __builtin_acosf(dir.y);
    f32 u = (phi + 3.14159265358979323846f) / (2.0f * 3.14159265358979323846f);
    f32 v = theta / 3.14159265358979323846f;
    return mel_vec2(u, v);
}
