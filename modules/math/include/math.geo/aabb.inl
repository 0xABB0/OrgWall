#ifdef _CLANGD
#pragma once
#include "aabb.h"
#endif

MEL_NODISCARD static inline Mel_AABB mel_aabb(Mel_Vec3 min, Mel_Vec3 max)
{
    return (Mel_AABB){ .min = min, .max = max };
}

MEL_NODISCARD static inline Mel_AABB mel_aabb_from_center_extents(Mel_Vec3 center, Mel_Vec3 extents)
{
    return (Mel_AABB){
        .min = mel_vec3_sub(center, extents),
        .max = mel_vec3_add(center, extents),
    };
}

MEL_NODISCARD static inline Mel_Vec3 mel_aabb_center(Mel_AABB a)
{
    return mel_vec3_scale(mel_vec3_add(a.min, a.max), 0.5f);
}

MEL_NODISCARD static inline Mel_Vec3 mel_aabb_extents(Mel_AABB a)
{
    return mel_vec3_scale(mel_vec3_sub(a.max, a.min), 0.5f);
}

MEL_NODISCARD static inline Mel_Vec3 mel_aabb_size(Mel_AABB a)
{
    return mel_vec3_sub(a.max, a.min);
}

MEL_NODISCARD static inline bool mel_aabb_contains_point(Mel_AABB a, Mel_Vec3 p)
{
    return p.x >= a.min.x && p.x <= a.max.x
        && p.y >= a.min.y && p.y <= a.max.y
        && p.z >= a.min.z && p.z <= a.max.z;
}

MEL_NODISCARD static inline bool mel_aabb_overlaps(Mel_AABB a, Mel_AABB b)
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x
        && a.min.y <= b.max.y && a.max.y >= b.min.y
        && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

MEL_NODISCARD static inline Mel_AABB mel_aabb_expand_point(Mel_AABB a, Mel_Vec3 p)
{
    return (Mel_AABB){
        .min = mel_vec3_min(a.min, p),
        .max = mel_vec3_max(a.max, p),
    };
}

MEL_NODISCARD static inline Mel_AABB mel_aabb_merge(Mel_AABB a, Mel_AABB b)
{
    return (Mel_AABB){
        .min = mel_vec3_min(a.min, b.min),
        .max = mel_vec3_max(a.max, b.max),
    };
}

MEL_NODISCARD static inline Mel_Vec3 mel_aabb_closest_point(Mel_AABB a, Mel_Vec3 p)
{
    return mel_vec3_min(mel_vec3_max(p, a.min), a.max);
}

MEL_NODISCARD static inline f32 mel_aabb_distance_to_point(Mel_AABB a, Mel_Vec3 p)
{
    Mel_Vec3 closest = mel_aabb_closest_point(a, p);
    return mel_vec3_dist(p, closest);
}
