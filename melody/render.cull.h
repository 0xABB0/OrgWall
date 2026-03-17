#pragma once

#include "math.vec3.h"
#include "math.vec4.h"
#include "math.geo.plane.h"
#include "collection.bitset.h"

typedef struct Mel_AABB Mel_AABB;
struct Mel_AABB
{
    Mel_Vec3 center;
    Mel_Vec3 extents;
};

typedef struct Mel_Frustum Mel_Frustum;
struct Mel_Frustum
{
    Mel_Plane planes[6];
};

[[nodiscard]] static inline bool mel_aabb_vs_plane(Mel_AABB aabb, Mel_Plane plane)
{
    Mel_Vec3 normal = mel_plane_normal(plane);
    f32 d = mel_plane_distance(plane);
    f32 r = mel_vec3_dot(mel_vec3_abs(normal), aabb.extents);
    f32 s = mel_vec3_dot(normal, aabb.center) + d;
    return s + r >= 0.0f;
}

[[nodiscard]] static inline bool mel_aabb_vs_frustum(Mel_AABB aabb, const Mel_Frustum* frustum)
{
    for (i32 i = 0; i < 6; i++)
    {
        if (!mel_aabb_vs_plane(aabb, frustum->planes[i]))
            return false;
    }
    return true;
}

static inline void mel_frustum_cull(const Mel_AABB* bounds, u32 count,
                                     const Mel_Frustum* frustum, Mel_BitSet* visibility)
{
    assert(bounds != nullptr);
    assert(frustum != nullptr);
    assert(visibility != nullptr);
    assert(visibility->bit_count >= count);

    mel_bitset_clear(visibility);

    for (u32 i = 0; i < count; i++)
    {
        if (mel_aabb_vs_frustum(bounds[i], frustum))
            mel_bitset_set(visibility, i);
    }
}
