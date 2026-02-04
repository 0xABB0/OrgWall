#ifdef _CLANGD
#pragma once
#include "quat.h"
#endif

static inline Mel_Quat mel_quat(f32 x, f32 y, f32 z, f32 w)
{
    return (Mel_Quat){ .v = (f32x4){x, y, z, w} };
}

static inline Mel_Quat mel_quat_from_axis_angle(Mel_Vec3 axis, f32 radians)
{
    f32 half = radians * 0.5f;
    f32 s = __builtin_sinf(half);
    f32 c = __builtin_cosf(half);
    Mel_Vec3 n = mel_vec3_normalize(axis);
    return (Mel_Quat){ .v = (f32x4){n.x * s, n.y * s, n.z * s, c} };
}

static inline Mel_Quat mel_quat_from_euler(f32 pitch, f32 yaw, f32 roll)
{
    f32 hp = pitch * 0.5f;
    f32 hy = yaw * 0.5f;
    f32 hr = roll * 0.5f;

    f32 cp = __builtin_cosf(hp);
    f32 sp = __builtin_sinf(hp);
    f32 cy = __builtin_cosf(hy);
    f32 sy = __builtin_sinf(hy);
    f32 cr = __builtin_cosf(hr);
    f32 sr = __builtin_sinf(hr);

    return (Mel_Quat){ .v = (f32x4){
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        cr * cp * cy + sr * sp * sy
    }};
}

static inline Mel_Quat mel_quat_mul(Mel_Quat a, Mel_Quat b)
{
    return (Mel_Quat){ .v = (f32x4){
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    }};
}

static inline Mel_Quat mel_quat_conjugate(Mel_Quat q)
{
    return (Mel_Quat){ .v = (f32x4){-q.x, -q.y, -q.z, q.w} };
}

static inline f32 mel_quat_len_sq(Mel_Quat q)
{
    f32x4 m = q.v * q.v;
    return m[0] + m[1] + m[2] + m[3];
}

static inline f32 mel_quat_len(Mel_Quat q)
{
    return __builtin_sqrtf(mel_quat_len_sq(q));
}

static inline Mel_Quat mel_quat_normalize(Mel_Quat q)
{
    f32 len = mel_quat_len(q);
    assert(len > 0.0f);
    return (Mel_Quat){ .v = q.v / len };
}

static inline Mel_Quat mel_quat_inverse(Mel_Quat q)
{
    f32 len_sq = mel_quat_len_sq(q);
    assert(len_sq > 0.0f);
    Mel_Quat conj = mel_quat_conjugate(q);
    return (Mel_Quat){ .v = conj.v / len_sq };
}

static inline Mel_Vec3 mel_quat_rotate_vec3(Mel_Quat q, Mel_Vec3 v)
{
    Mel_Vec3 qv = mel_vec3(q.x, q.y, q.z);
    Mel_Vec3 uv = mel_vec3_cross(qv, v);
    Mel_Vec3 uuv = mel_vec3_cross(qv, uv);
    uv = mel_vec3_scale(uv, 2.0f * q.w);
    uuv = mel_vec3_scale(uuv, 2.0f);
    return mel_vec3_add(v, mel_vec3_add(uv, uuv));
}

static inline Mel_Quat mel_quat_slerp(Mel_Quat a, Mel_Quat b, f32 t)
{
    f32x4 am = a.v * b.v;
    f32 dot = am[0] + am[1] + am[2] + am[3];

    if (dot < 0.0f)
    {
        b.v = -b.v;
        dot = -dot;
    }

    if (dot > 0.9995f)
    {
        Mel_Quat result = { .v = a.v + (b.v - a.v) * t };
        return mel_quat_normalize(result);
    }

    f32 theta0 = __builtin_acosf(dot);
    f32 theta = theta0 * t;
    f32 sin_theta = __builtin_sinf(theta);
    f32 sin_theta0 = __builtin_sinf(theta0);

    f32 s0 = __builtin_cosf(theta) - dot * sin_theta / sin_theta0;
    f32 s1 = sin_theta / sin_theta0;

    return (Mel_Quat){ .v = a.v * s0 + b.v * s1 };
}

static inline Mel_Mat4 mel_quat_to_mat4(Mel_Quat q)
{
    f32 xx = q.x * q.x;
    f32 yy = q.y * q.y;
    f32 zz = q.z * q.z;
    f32 xy = q.x * q.y;
    f32 xz = q.x * q.z;
    f32 yz = q.y * q.z;
    f32 wx = q.w * q.x;
    f32 wy = q.w * q.y;
    f32 wz = q.w * q.z;

    return (Mel_Mat4){ .rows = {
        (f32x4){1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz),        2.0f * (xz + wy),        0},
        (f32x4){2.0f * (xy + wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx),        0},
        (f32x4){2.0f * (xz - wy),        2.0f * (yz + wx),        1.0f - 2.0f * (xx + yy), 0},
        (f32x4){0,                       0,                       0,                       1}
    }};
}
