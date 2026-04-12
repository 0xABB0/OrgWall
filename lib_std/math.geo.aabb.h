#pragma once

#include "math.vec3.h"

typedef struct {
    Mel_Vec3 min;
    Mel_Vec3 max;
} Mel_AABB;

[[nodiscard]] static inline Mel_AABB mel_aabb(Mel_Vec3 min, Mel_Vec3 max);
[[nodiscard]] static inline Mel_AABB mel_aabb_from_center_extents(Mel_Vec3 center, Mel_Vec3 extents);
[[nodiscard]] static inline Mel_Vec3 mel_aabb_center(Mel_AABB a);
[[nodiscard]] static inline Mel_Vec3 mel_aabb_extents(Mel_AABB a);
[[nodiscard]] static inline Mel_Vec3 mel_aabb_size(Mel_AABB a);
[[nodiscard]] static inline bool mel_aabb_contains_point(Mel_AABB a, Mel_Vec3 p);
[[nodiscard]] static inline bool mel_aabb_overlaps(Mel_AABB a, Mel_AABB b);
[[nodiscard]] static inline Mel_AABB mel_aabb_expand_point(Mel_AABB a, Mel_Vec3 p);
[[nodiscard]] static inline Mel_AABB mel_aabb_merge(Mel_AABB a, Mel_AABB b);
[[nodiscard]] static inline Mel_Vec3 mel_aabb_closest_point(Mel_AABB a, Mel_Vec3 p);
[[nodiscard]] static inline f32 mel_aabb_distance_to_point(Mel_AABB a, Mel_Vec3 p);

#include "math.geo.aabb.inl"
