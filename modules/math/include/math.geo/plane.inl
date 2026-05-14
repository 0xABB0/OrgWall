#ifdef _CLANGD
#pragma once
#include "plane.h"
#endif

static inline Mel_Vec3 mel_plane_normal(Mel_Plane p)
{
    return mel_vec3(p.x, p.y, p.z);
}

static inline f32 mel_plane_distance(Mel_Plane p)
{
    return p.w;
}

static inline Mel_Plane mel_plane_from_normal_point(Mel_Vec3 normal, Mel_Vec3 point)
{
    return mel_plane(normal.x, normal.y, normal.z, -mel_vec3_dot(normal, point));
}

static inline Mel_Plane mel_plane_from_points(Mel_Vec3 a, Mel_Vec3 b, Mel_Vec3 c)
{
    Mel_Vec3 normal = mel_vec3_normalize(mel_vec3_cross(mel_vec3_sub(b, a), mel_vec3_sub(c, a)));
    return mel_plane_from_normal_point(normal, a);
}

static inline Mel_Plane mel_plane_normalize(Mel_Plane p)
{
    f32 len = mel_vec3_len(mel_plane_normal(p));
    assert(len > 0.0f);
    f32 inv = 1.0f / len;
    return mel_plane(p.x * inv, p.y * inv, p.z * inv, p.w * inv);
}

static inline f32 mel_plane_dist_to_point(Mel_Plane p, Mel_Vec3 point)
{
    return mel_vec3_dot(mel_plane_normal(p), point) + p.w;
}

static inline Mel_Vec3 mel_plane_project_point(Mel_Plane p, Mel_Vec3 point)
{
    return mel_vec3_sub(point, mel_vec3_scale(mel_plane_normal(p), mel_plane_dist_to_point(p, point)));
}
