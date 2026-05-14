#ifdef _CLANGD
#pragma once
#include "mat3.h"
#endif

static inline Mel_Mat3 mel_mat3_mul(Mel_Mat3 a, Mel_Mat3 b)
{
    Mel_Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

static inline Mel_Vec3 mel_mat3_mul_vec3(Mel_Mat3 m, Mel_Vec3 v)
{
    return mel_vec3(
        m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
        m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
        m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z
    );
}

static inline Mel_Vec2 mel_mat3_mul_vec2(Mel_Mat3 m, Mel_Vec2 v)
{
    return mel_vec2(
        m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2],
        m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2]
    );
}

static inline Mel_Mat3 mel_mat3_mul_inverse(Mel_Mat3 a, Mel_Mat3 b)
{
    return mel_mat3_mul(a, mel_mat3_transpose(b));
}

static inline Mel_Vec3 mel_mat3_mul_vec3_inverse(Mel_Mat3 m, Mel_Vec3 v)
{
    return mel_mat3_mul_vec3(mel_mat3_transpose(m), v);
}

static inline Mel_Mat3 mel_mat3_transpose(Mel_Mat3 m)
{
    return (Mel_Mat3){ .m = {
        {m.m[0][0], m.m[1][0], m.m[2][0]},
        {m.m[0][1], m.m[1][1], m.m[2][1]},
        {m.m[0][2], m.m[1][2], m.m[2][2]}
    }};
}

static inline f32 mel_mat3_determinant(Mel_Mat3 m)
{
    return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1])
         - m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0])
         + m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
}

static inline Mel_Mat3 mel_mat3_inverse(Mel_Mat3 m)
{
    f32 det = mel_mat3_determinant(m);
    f32 inv_det = 1.0f / det;
    return (Mel_Mat3){ .m = {
        {(m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) * inv_det,
         (m.m[0][2] * m.m[2][1] - m.m[0][1] * m.m[2][2]) * inv_det,
         (m.m[0][1] * m.m[1][2] - m.m[0][2] * m.m[1][1]) * inv_det},
        {(m.m[1][2] * m.m[2][0] - m.m[1][0] * m.m[2][2]) * inv_det,
         (m.m[0][0] * m.m[2][2] - m.m[0][2] * m.m[2][0]) * inv_det,
         (m.m[0][2] * m.m[1][0] - m.m[0][0] * m.m[1][2]) * inv_det},
        {(m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]) * inv_det,
         (m.m[0][1] * m.m[2][0] - m.m[0][0] * m.m[2][1]) * inv_det,
         (m.m[0][0] * m.m[1][1] - m.m[0][1] * m.m[1][0]) * inv_det}
    }};
}

static inline Mel_Mat3 mel_mat3_rotate(f32 radians)
{
    f32 c = __builtin_cosf(radians);
    f32 s = __builtin_sinf(radians);
    return (Mel_Mat3){ .m = {
        { c, -s, 0},
        { s,  c, 0},
        { 0,  0, 1}
    }};
}

static inline Mel_Mat3 mel_mat3_scale_2d(f32 sx, f32 sy)
{
    return (Mel_Mat3){ .m = {
        {sx,  0, 0},
        { 0, sy, 0},
        { 0,  0, 1}
    }};
}

static inline Mel_Mat3 mel_mat3_translate_2d(f32 x, f32 y)
{
    return (Mel_Mat3){ .m = {
        {1, 0, x},
        {0, 1, y},
        {0, 0, 1}
    }};
}

static inline Mel_Mat3 mel_mat3_translate_2dv(Mel_Vec2 v)
{
    return mel_mat3_translate_2d(v.x, v.y);
}

static inline Mel_Mat3 mel_mat3_SRT(f32 sx, f32 sy, f32 angle, f32 tx, f32 ty)
{
    f32 c = __builtin_cosf(angle);
    f32 s = __builtin_sinf(angle);
    return (Mel_Mat3){ .m = {
        {sx * c, -sy * s, tx},
        {sx * s,  sy * c, ty},
        {    0,       0,   1}
    }};
}

static inline Mel_Mat3 mel_mat3_from_mat4(Mel_Mat4 m)
{
    return (Mel_Mat3){ .m = {
        {m.m[0][0], m.m[0][1], m.m[0][2]},
        {m.m[1][0], m.m[1][1], m.m[1][2]},
        {m.m[2][0], m.m[2][1], m.m[2][2]}
    }};
}
