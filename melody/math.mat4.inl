#ifdef _CLANGD
#pragma once
#include "math.mat4.h"
#endif

static inline Mel_Mat4 mel_mat4_mul(Mel_Mat4 a, Mel_Mat4 b)
{
    Mel_Mat4 result;
    for (int i = 0; i < 4; i++)
    {
        f32x4 row = a.rows[i];
        result.rows[i] =
            row[0] * b.rows[0] +
            row[1] * b.rows[1] +
            row[2] * b.rows[2] +
            row[3] * b.rows[3];
    }
    return result;
}

static inline Mel_Vec4 mel_mat4_mul_vec4(Mel_Mat4 m, Mel_Vec4 v)
{
    return (Mel_Vec4){ .v = (f32x4){
        mel_vec4_dot((Mel_Vec4){.v = m.rows[0]}, v),
        mel_vec4_dot((Mel_Vec4){.v = m.rows[1]}, v),
        mel_vec4_dot((Mel_Vec4){.v = m.rows[2]}, v),
        mel_vec4_dot((Mel_Vec4){.v = m.rows[3]}, v)
    }};
}

static inline Mel_Vec3 mel_mat4_mul_point(Mel_Mat4 m, Mel_Vec3 p)
{
    Mel_Vec4 v = mel_mat4_mul_vec4(m, mel_vec4(p.x, p.y, p.z, 1.0f));
    return mel_vec3(v.x, v.y, v.z);
}

static inline Mel_Vec3 mel_mat4_mul_dir(Mel_Mat4 m, Mel_Vec3 d)
{
    Mel_Vec4 v = mel_mat4_mul_vec4(m, mel_vec4(d.x, d.y, d.z, 0.0f));
    return mel_vec3(v.x, v.y, v.z);
}

static inline Mel_Mat4 mel_mat4_transpose(Mel_Mat4 m)
{
    return (Mel_Mat4){ .rows = {
        (f32x4){m.m[0][0], m.m[1][0], m.m[2][0], m.m[3][0]},
        (f32x4){m.m[0][1], m.m[1][1], m.m[2][1], m.m[3][1]},
        (f32x4){m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2]},
        (f32x4){m.m[0][3], m.m[1][3], m.m[2][3], m.m[3][3]}
    }};
}

static inline Mel_Mat4 mel_mat4_translate(Mel_Vec3 t)
{
    return (Mel_Mat4){ .rows = {
        (f32x4){1, 0, 0, t.x},
        (f32x4){0, 1, 0, t.y},
        (f32x4){0, 0, 1, t.z},
        (f32x4){0, 0, 0, 1}
    }};
}

static inline Mel_Mat4 mel_mat4_scale(Mel_Vec3 s)
{
    return (Mel_Mat4){ .rows = {
        (f32x4){s.x, 0,   0,   0},
        (f32x4){0,   s.y, 0,   0},
        (f32x4){0,   0,   s.z, 0},
        (f32x4){0,   0,   0,   1}
    }};
}

static inline Mel_Mat4 mel_mat4_rotate_x(f32 radians)
{
    f32 c = __builtin_cosf(radians);
    f32 s = __builtin_sinf(radians);
    return (Mel_Mat4){ .rows = {
        (f32x4){1, 0,  0, 0},
        (f32x4){0, c, -s, 0},
        (f32x4){0, s,  c, 0},
        (f32x4){0, 0,  0, 1}
    }};
}

static inline Mel_Mat4 mel_mat4_rotate_y(f32 radians)
{
    f32 c = __builtin_cosf(radians);
    f32 s = __builtin_sinf(radians);
    return (Mel_Mat4){ .rows = {
        (f32x4){ c, 0, s, 0},
        (f32x4){ 0, 1, 0, 0},
        (f32x4){-s, 0, c, 0},
        (f32x4){ 0, 0, 0, 1}
    }};
}

static inline Mel_Mat4 mel_mat4_rotate_z(f32 radians)
{
    f32 c = __builtin_cosf(radians);
    f32 s = __builtin_sinf(radians);
    return (Mel_Mat4){ .rows = {
        (f32x4){c, -s, 0, 0},
        (f32x4){s,  c, 0, 0},
        (f32x4){0,  0, 1, 0},
        (f32x4){0,  0, 0, 1}
    }};
}

static inline Mel_Mat4 mel_mat4_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far)
{
    f32 rl = 1.0f / (right - left);
    f32 tb = 1.0f / (top - bottom);
    f32 fn = 1.0f / (far - near);

    return (Mel_Mat4){ .rows = {
        (f32x4){2.0f * rl,       0,                0,           -(right + left) * rl},
        (f32x4){0,               2.0f * tb,        0,           -(top + bottom) * tb},
        (f32x4){0,               0,               -fn,          -near * fn},
        (f32x4){0,               0,                0,            1}
    }};
}

static inline Mel_Mat4 mel_mat4_perspective(f32 fov_radians, f32 aspect, f32 near, f32 far)
{
    f32 t = __builtin_tanf(fov_radians * 0.5f);
    f32 fn = 1.0f / (far - near);

    return (Mel_Mat4){ .rows = {
        (f32x4){1.0f / (aspect * t), 0,         0,                       0},
        (f32x4){0,                   1.0f / t,  0,                       0},
        (f32x4){0,                   0,        -far * fn,               -far * near * fn},
        (f32x4){0,                   0,        -1,                       0}
    }};
}

static inline Mel_Mat4 mel_mat4_look_at(Mel_Vec3 eye, Mel_Vec3 target, Mel_Vec3 up)
{
    Mel_Vec3 f = mel_vec3_normalize(mel_vec3_sub(target, eye));
    Mel_Vec3 r = mel_vec3_normalize(mel_vec3_cross(f, up));
    Mel_Vec3 u = mel_vec3_cross(r, f);

    return (Mel_Mat4){ .rows = {
        (f32x4){ r.x,  r.y,  r.z, -mel_vec3_dot(r, eye)},
        (f32x4){ u.x,  u.y,  u.z, -mel_vec3_dot(u, eye)},
        (f32x4){-f.x, -f.y, -f.z,  mel_vec3_dot(f, eye)},
        (f32x4){ 0,    0,    0,    1}
    }};
}

static inline Mel_Mat4 mel_mat4_scalef(f32 s)
{
    return mel_mat4_scale(mel_vec3(s, s, s));
}

static inline Mel_Mat4 mel_mat4_rotateXY(f32 ax, f32 ay)
{
    return mel_mat4_mul(mel_mat4_rotate_x(ax), mel_mat4_rotate_y(ay));
}

static inline Mel_Mat4 mel_mat4_rotateXYZ(f32 ax, f32 ay, f32 az)
{
    return mel_mat4_mul(mel_mat4_mul(mel_mat4_rotate_x(ax), mel_mat4_rotate_y(ay)), mel_mat4_rotate_z(az));
}

static inline Mel_Mat4 mel_mat4_rotateZYX(f32 ax, f32 ay, f32 az)
{
    return mel_mat4_mul(mel_mat4_mul(mel_mat4_rotate_z(az), mel_mat4_rotate_y(ay)), mel_mat4_rotate_x(ax));
}

static inline Mel_Vec3 mel_mat4_mul_vec3(Mel_Mat4 m, Mel_Vec3 v)
{
    return mel_mat4_mul_point(m, v);
}

static inline Mel_Vec3 mel_mat4_mul_vec3_xyz0(Mel_Mat4 m, Mel_Vec3 v)
{
    return mel_mat4_mul_dir(m, v);
}

static inline Mel_Vec3 mel_mat4_mul_vec3_H(Mel_Mat4 m, Mel_Vec3 v)
{
    Mel_Vec4 r = mel_mat4_mul_vec4(m, mel_vec4(v.x, v.y, v.z, 1.0f));
    f32 inv_w = 1.0f / r.w;
    return mel_vec3(r.x * inv_w, r.y * inv_w, r.z * inv_w);
}

static inline Mel_Mat4 mel_mat4_SRT(f32 sx, f32 sy, f32 sz, f32 ax, f32 ay, f32 az, f32 tx, f32 ty, f32 tz)
{
    return mel_mat4_mul(mel_mat4_mul(mel_mat4_translate(mel_vec3(tx, ty, tz)), mel_mat4_rotateZYX(ax, ay, az)), mel_mat4_scale(mel_vec3(sx, sy, sz)));
}

static inline Mel_Mat4 mel_mat4_inv_transform(Mel_Mat4 m)
{
    Mel_Mat4 r;
    r.rows[0] = (f32x4){m.m[0][0], m.m[1][0], m.m[2][0], 0};
    r.rows[1] = (f32x4){m.m[0][1], m.m[1][1], m.m[2][1], 0};
    r.rows[2] = (f32x4){m.m[0][2], m.m[1][2], m.m[2][2], 0};

    Mel_Vec3 t = mel_vec3(m.m[0][3], m.m[1][3], m.m[2][3]);
    r.m[0][3] = -(r.m[0][0] * t.x + r.m[0][1] * t.y + r.m[0][2] * t.z);
    r.m[1][3] = -(r.m[1][0] * t.x + r.m[1][1] * t.y + r.m[1][2] * t.z);
    r.m[2][3] = -(r.m[2][0] * t.x + r.m[2][1] * t.y + r.m[2][2] * t.z);
    r.rows[3] = (f32x4){0, 0, 0, 1};
    return r;
}

static inline Mel_Mat4 mel_mat4_proj_flip_handedness(Mel_Mat4 m)
{
    Mel_Mat4 r = m;
    r.m[2][0] = -r.m[2][0];
    r.m[2][1] = -r.m[2][1];
    r.m[2][2] = -r.m[2][2];
    r.m[2][3] = -r.m[2][3];
    return r;
}

static inline Mel_Mat4 mel_mat4_view_flip_handedness(Mel_Mat4 m)
{
    Mel_Mat4 r = m;
    r.m[0][2] = -r.m[0][2];
    r.m[1][2] = -r.m[1][2];
    r.m[2][0] = -r.m[2][0];
    r.m[2][1] = -r.m[2][1];
    r.m[2][3] = -r.m[2][3];
    return r;
}
