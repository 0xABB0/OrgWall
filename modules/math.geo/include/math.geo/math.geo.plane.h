#pragma once

#include <core/compiler.h>

#include "math.vec3.h"
#include "math.vec4.h"

typedef Mel_Vec4 Mel_Plane;

#define mel_plane(nx, ny, nz, d) mel_vec4((nx), (ny), (nz), (d))

MEL_NODISCARD static inline Mel_Vec3 mel_plane_normal(Mel_Plane p);
MEL_NODISCARD static inline f32 mel_plane_distance(Mel_Plane p);
MEL_NODISCARD static inline Mel_Plane mel_plane_from_normal_point(Mel_Vec3 normal, Mel_Vec3 point);
MEL_NODISCARD static inline Mel_Plane mel_plane_from_points(Mel_Vec3 a, Mel_Vec3 b, Mel_Vec3 c);
MEL_NODISCARD static inline Mel_Plane mel_plane_normalize(Mel_Plane p);
MEL_NODISCARD static inline f32 mel_plane_dist_to_point(Mel_Plane p, Mel_Vec3 point);
MEL_NODISCARD static inline Mel_Vec3 mel_plane_project_point(Mel_Plane p, Mel_Vec3 point);

#include "math.geo.plane.inl"
