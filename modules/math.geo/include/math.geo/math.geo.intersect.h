#pragma once

#include <core/compiler.h>

#include "math.vec3.h"
#include "math.geo.ray.h"
#include "math.geo.sphere.h"
#include "math.geo.aabb.h"
#include "math.geo.plane.h"

typedef struct {
    f32 t;
    Mel_Vec3 point;
    Mel_Vec3 normal;
    bool hit;
} Mel_Raycast_Hit;

MEL_NODISCARD static inline Mel_Raycast_Hit mel_ray_vs_aabb(Mel_Ray ray, Mel_AABB aabb);
MEL_NODISCARD static inline Mel_Raycast_Hit mel_ray_vs_sphere(Mel_Ray ray, Mel_Sphere sphere);
MEL_NODISCARD static inline Mel_Raycast_Hit mel_ray_vs_plane(Mel_Ray ray, Mel_Plane plane);

MEL_NODISCARD static inline bool mel_sphere_vs_sphere(Mel_Sphere a, Mel_Sphere b);
MEL_NODISCARD static inline bool mel_sphere_vs_aabb(Mel_Sphere s, Mel_AABB a);
MEL_NODISCARD static inline bool mel_aabb_vs_aabb(Mel_AABB a, Mel_AABB b);

MEL_NODISCARD static inline bool mel_point_in_sphere(Mel_Vec3 p, Mel_Sphere s);
MEL_NODISCARD static inline bool mel_point_in_aabb(Mel_Vec3 p, Mel_AABB a);

MEL_NODISCARD static inline Mel_Vec3 mel_closest_point_on_sphere(Mel_Vec3 p, Mel_Sphere s);

#include "math.geo.intersect.inl"
