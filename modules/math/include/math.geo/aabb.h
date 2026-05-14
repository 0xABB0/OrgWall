#pragma once

#include <core/compiler.h>

#include <math.vector/vec3.h>

typedef struct {
    Mel_Vec3 min;
    Mel_Vec3 max;
} Mel_AABB;

MEL_NODISCARD static inline Mel_AABB mel_aabb(Mel_Vec3 min, Mel_Vec3 max);
MEL_NODISCARD static inline Mel_AABB mel_aabb_from_center_extents(Mel_Vec3 center, Mel_Vec3 extents);
MEL_NODISCARD static inline Mel_Vec3 mel_aabb_center(Mel_AABB a);
MEL_NODISCARD static inline Mel_Vec3 mel_aabb_extents(Mel_AABB a);
MEL_NODISCARD static inline Mel_Vec3 mel_aabb_size(Mel_AABB a);
MEL_NODISCARD static inline bool mel_aabb_contains_point(Mel_AABB a, Mel_Vec3 p);
MEL_NODISCARD static inline bool mel_aabb_overlaps(Mel_AABB a, Mel_AABB b);
MEL_NODISCARD static inline Mel_AABB mel_aabb_expand_point(Mel_AABB a, Mel_Vec3 p);
MEL_NODISCARD static inline Mel_AABB mel_aabb_merge(Mel_AABB a, Mel_AABB b);
MEL_NODISCARD static inline Mel_Vec3 mel_aabb_closest_point(Mel_AABB a, Mel_Vec3 p);
MEL_NODISCARD static inline f32 mel_aabb_distance_to_point(Mel_AABB a, Mel_Vec3 p);

#include "aabb.inl"
