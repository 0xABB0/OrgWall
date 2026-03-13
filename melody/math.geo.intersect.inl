#ifdef _CLANGD
#pragma once
#include "math.geo.intersect.h"
#endif

[[nodiscard]] static inline Mel_Raycast_Hit mel_ray_vs_aabb(Mel_Ray ray, Mel_AABB aabb)
{
    Mel_Vec3 inv_dir = mel_vec3_rcp(ray.dir);

    f32 t1 = (aabb.min.x - ray.origin.x) * inv_dir.x;
    f32 t2 = (aabb.max.x - ray.origin.x) * inv_dir.x;
    f32 t3 = (aabb.min.y - ray.origin.y) * inv_dir.y;
    f32 t4 = (aabb.max.y - ray.origin.y) * inv_dir.y;
    f32 t5 = (aabb.min.z - ray.origin.z) * inv_dir.z;
    f32 t6 = (aabb.max.z - ray.origin.z) * inv_dir.z;

    f32 tmin = fmaxf(fmaxf(fminf(t1, t2), fminf(t3, t4)), fminf(t5, t6));
    f32 tmax = fminf(fminf(fmaxf(t1, t2), fmaxf(t3, t4)), fmaxf(t5, t6));

    if (tmax < 0 || tmin > tmax)
        return (Mel_Raycast_Hit){ .hit = false };

    f32 t = tmin >= 0 ? tmin : tmax;
    Mel_Vec3 point = mel_ray_at(ray, t);

    Mel_Vec3 center = mel_aabb_center(aabb);
    Mel_Vec3 d = mel_vec3_sub(point, center);
    Mel_Vec3 ext = mel_aabb_extents(aabb);

    f32 ax = fabsf(d.x / ext.x);
    f32 ay = fabsf(d.y / ext.y);
    f32 az = fabsf(d.z / ext.z);

    Mel_Vec3 normal = MEL_VEC3_ZERO;
    if (ax > ay && ax > az)
        normal.x = d.x > 0 ? 1.0f : -1.0f;
    else if (ay > az)
        normal.y = d.y > 0 ? 1.0f : -1.0f;
    else
        normal.z = d.z > 0 ? 1.0f : -1.0f;

    return (Mel_Raycast_Hit){ .t = t, .point = point, .normal = normal, .hit = true };
}

[[nodiscard]] static inline Mel_Raycast_Hit mel_ray_vs_sphere(Mel_Ray ray, Mel_Sphere sphere)
{
    Mel_Vec3 oc = mel_vec3_sub(ray.origin, sphere.center);
    f32 b = mel_vec3_dot(oc, ray.dir);
    f32 c = mel_vec3_dot(oc, oc) - sphere.radius * sphere.radius;
    f32 discriminant = b * b - c;

    if (discriminant < 0)
        return (Mel_Raycast_Hit){ .hit = false };

    f32 sqrt_d = sqrtf(discriminant);
    f32 t = -b - sqrt_d;
    if (t < 0) {
        t = -b + sqrt_d;
        if (t < 0)
            return (Mel_Raycast_Hit){ .hit = false };
    }

    Mel_Vec3 point = mel_ray_at(ray, t);
    Mel_Vec3 normal = mel_vec3_normalize(mel_vec3_sub(point, sphere.center));
    return (Mel_Raycast_Hit){ .t = t, .point = point, .normal = normal, .hit = true };
}

[[nodiscard]] static inline Mel_Raycast_Hit mel_ray_vs_plane(Mel_Ray ray, Mel_Plane plane)
{
    Mel_Vec3 n = mel_plane_normal(plane);
    f32 denom = mel_vec3_dot(n, ray.dir);

    if (fabsf(denom) < 1e-7f)
        return (Mel_Raycast_Hit){ .hit = false };

    f32 t = -(mel_vec3_dot(n, ray.origin) + mel_plane_distance(plane)) / denom;
    if (t < 0)
        return (Mel_Raycast_Hit){ .hit = false };

    Mel_Vec3 point = mel_ray_at(ray, t);
    return (Mel_Raycast_Hit){ .t = t, .point = point, .normal = n, .hit = true };
}

[[nodiscard]] static inline bool mel_sphere_vs_sphere(Mel_Sphere a, Mel_Sphere b)
{
    f32 r = a.radius + b.radius;
    return mel_vec3_dist_sq(a.center, b.center) <= r * r;
}

[[nodiscard]] static inline bool mel_sphere_vs_aabb(Mel_Sphere s, Mel_AABB a)
{
    f32 d = mel_aabb_distance_to_point(a, s.center);
    return d <= s.radius;
}

[[nodiscard]] static inline bool mel_aabb_vs_aabb(Mel_AABB a, Mel_AABB b)
{
    return mel_aabb_overlaps(a, b);
}

[[nodiscard]] static inline bool mel_point_in_sphere(Mel_Vec3 p, Mel_Sphere s)
{
    return mel_vec3_dist_sq(p, s.center) <= s.radius * s.radius;
}

[[nodiscard]] static inline bool mel_point_in_aabb(Mel_Vec3 p, Mel_AABB a)
{
    return mel_aabb_contains_point(a, p);
}

[[nodiscard]] static inline Mel_Vec3 mel_closest_point_on_sphere(Mel_Vec3 p, Mel_Sphere s)
{
    Mel_Vec3 dir = mel_vec3_sub(p, s.center);
    f32 len = mel_vec3_len(dir);
    if (len < 1e-7f)
        return mel_vec3_add(s.center, mel_vec3_scale(MEL_VEC3_UP, s.radius));
    return mel_vec3_add(s.center, mel_vec3_scale(dir, s.radius / len));
}
