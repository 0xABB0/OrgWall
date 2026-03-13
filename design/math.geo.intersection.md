# math.geo.intersection — Ray & Geometry Intersection Tests

## Problem

Melody has basic geometry types (`Rect`, `iRect`, `Plane`, `Point2`, `Point3`) but no ray type and no intersection tests beyond rect overlap and plane distance. This blocks: mouse picking, visibility queries, physics raycasts, debug visualization hit testing, frustum culling, and any spatial query.

## What This Module Provides

- New geometry types: Ray, Sphere, AABB (3D)
- Intersection tests for common pairs
- Frustum extraction from view-projection matrix + culling tests
- Rich hit results (distance, point, normal)

## Inspiration

- **Animation-Engine**: `src/Math/Geometry/Geometry.h` — Segment, Ray, Triangle, AABB, Sphere
- **Animation-Engine**: `src/Math/Geometry/IntersectionTests.h` — ray_vs_aabb, point_in_aabb_2d
- Reference: Real-Time Collision Detection (Ericson)

## File Layout

```
math.geo.ray.h         — Ray type + inline construction
math.geo.sphere.h      — Sphere type + inline construction
math.geo.aabb.h        — AABB3 type + inline construction/queries
math.geo.aabb.inl      — inline implementations
math.geo.frustum.h     — Frustum type (6 planes) + extraction + culling
math.geo.frustum.inl   — inline implementations
math.geo.intersect.h   — intersection test declarations
math.geo.intersect.inl — inline intersection implementations
```

Following the existing `math.geo.*` pattern. Each primitive gets its own files. Intersection tests go in a shared `intersect` module since they cross-reference multiple types.

---

## Types

### Mel_Ray

```c
typedef struct {
    Mel_Vec3 origin;
    Mel_Vec3 dir;
} Mel_Ray;

static inline Mel_Ray mel_ray(Mel_Vec3 origin, Mel_Vec3 dir);
static inline Mel_Vec3 mel_ray_at(Mel_Ray r, f32 t);
```

`dir` is expected to be normalized. `mel_ray_at` returns `origin + dir * t`.

### Mel_Sphere

```c
typedef struct {
    Mel_Vec3 center;
    f32 radius;
} Mel_Sphere;

static inline Mel_Sphere mel_sphere(Mel_Vec3 center, f32 radius);
```

### Mel_AABB (3D)

```c
typedef struct {
    Mel_Vec3 min;
    Mel_Vec3 max;
} Mel_AABB;

static inline Mel_AABB mel_aabb(Mel_Vec3 min, Mel_Vec3 max);
static inline Mel_AABB mel_aabb_from_center_extents(Mel_Vec3 center, Mel_Vec3 extents);
static inline Mel_Vec3 mel_aabb_center(Mel_AABB a);
static inline Mel_Vec3 mel_aabb_extents(Mel_AABB a);
static inline Mel_Vec3 mel_aabb_size(Mel_AABB a);
static inline bool mel_aabb_contains_point(Mel_AABB a, Mel_Vec3 p);
static inline bool mel_aabb_overlaps(Mel_AABB a, Mel_AABB b);
static inline Mel_AABB mel_aabb_expand_point(Mel_AABB a, Mel_Vec3 p);
static inline Mel_AABB mel_aabb_merge(Mel_AABB a, Mel_AABB b);
```

Min/max representation. This differs from `Mel_Rect` (pos/size) — 3D AABBs are more naturally expressed as min/max because of the three dimensions. We considered matching Rect's convention but min/max is standard for 3D and simplifies intersection math.

### Mel_Frustum

```c
typedef struct {
    Mel_Plane planes[6];
} Mel_Frustum;

Mel_Frustum mel_frustum_from_vp(Mel_Mat4 vp);
```

Six planes extracted from the view-projection matrix (left, right, bottom, top, near, far). Normals point inward. Uses the Gribb-Hartmann extraction method.

### Hit result

```c
typedef struct {
    f32 t;
    Mel_Vec3 point;
    Mel_Vec3 normal;
    bool hit;
} Mel_Raycast_Hit;
```

`t` is the parametric distance along the ray. `point = ray.origin + ray.dir * t`. `normal` is the surface normal at the hit point. `hit` is false if no intersection.

---

## Intersection API

All intersection functions are pure — no state, no allocations.

### Ray intersections

```c
Mel_Raycast_Hit mel_ray_vs_aabb(Mel_Ray ray, Mel_AABB aabb);
Mel_Raycast_Hit mel_ray_vs_sphere(Mel_Ray ray, Mel_Sphere sphere);
Mel_Raycast_Hit mel_ray_vs_plane(Mel_Ray ray, Mel_Plane plane);
Mel_Raycast_Hit mel_ray_vs_triangle(Mel_Ray ray, Mel_Vec3 v0, Mel_Vec3 v1, Mel_Vec3 v2);
```

`ray_vs_aabb`: Slab method. Returns closest positive intersection.
`ray_vs_sphere`: Quadratic formula. Returns closest positive root.
`ray_vs_plane`: Dot product test. Returns single intersection.
`ray_vs_triangle`: Moller-Trumbore algorithm. Returns intersection with barycentric coords in normal (u, v stored in x, y for caller convenience — or we can add a triangle-specific hit type).

### Overlap tests (bool)

```c
bool mel_sphere_vs_sphere(Mel_Sphere a, Mel_Sphere b);
bool mel_sphere_vs_aabb(Mel_Sphere s, Mel_AABB a);
bool mel_aabb_vs_aabb(Mel_AABB a, Mel_AABB b);
```

These are fast boolean tests — no contact point computation.

### Frustum culling

```c
bool mel_frustum_contains_point(Mel_Frustum f, Mel_Vec3 p);
bool mel_frustum_vs_sphere(Mel_Frustum f, Mel_Sphere s);
bool mel_frustum_vs_aabb(Mel_Frustum f, Mel_AABB a);
```

All return true if the primitive is at least partially inside the frustum. The AABB test uses the "test against each plane, reject if fully outside any" approach (conservative — may return true for objects in corner regions outside the frustum, but no false negatives).

### Point tests

```c
bool mel_point_in_sphere(Mel_Vec3 p, Mel_Sphere s);
bool mel_point_in_aabb(Mel_Vec3 p, Mel_AABB a);
```

### Distance queries

```c
f32 mel_point_distance_to_aabb(Mel_Vec3 p, Mel_AABB a);
Mel_Vec3 mel_closest_point_on_aabb(Mel_Vec3 p, Mel_AABB a);
Mel_Vec3 mel_closest_point_on_sphere(Mel_Vec3 p, Mel_Sphere s);
```

---

## Design Decisions

**Why min/max for AABB instead of center/extents?**
Min/max simplifies overlap tests and point-in-AABB to direct comparisons. Center/extents requires adding/subtracting on every test. We provide `mel_aabb_from_center_extents` and `mel_aabb_center`/`mel_aabb_extents` for conversion.

**Why Mel_Raycast_Hit instead of out-params?**
A return struct is cleaner: `Mel_Raycast_Hit hit = mel_ray_vs_sphere(ray, sphere); if (hit.hit) { ... }`. No uninitialized out-param footguns. The struct is small (32 bytes) — returned by value is fine.

**Why inline intersection tests?**
Most of these are 10-20 lines of arithmetic. Inlining avoids function call overhead for tight loops (e.g., testing a ray against many AABBs). The `.inl` file keeps the header clean.

**Why no triangle mesh intersection?**
Single triangle intersection is provided. Mesh-level intersection requires an acceleration structure (BVH, octree) which is a separate data structure concern, not a math utility.

---

## Improvements Over Animation-Engine

1. Rich hit results (point + normal + distance) instead of just float or bool
2. Sphere, plane, and triangle ray tests (they only had ray_vs_aabb)
3. 3D AABB with full utility functions (contains, overlaps, merge, expand)
4. Frustum culling from VP matrix
5. Distance queries and closest-point functions
6. Separation between primitives (own files) and intersection tests

---

## Open Questions for Gabbo

1. **Triangle hit result**: should `mel_ray_vs_triangle` return barycentric coords? Useful for interpolating vertex attributes at the hit point. Could add `f32 u, v` to the hit struct, or use a separate `Mel_Triangle_Hit` type.

2. **Segment type**: the Animation-Engine has `Segment` (start + end). Do we need it, or is `Mel_Ray` + a max distance sufficient? A segment is just a bounded ray.

3. **Should `Mel_AABB` have its own `aabb.h` file or go into the existing `rect.h`?** They're conceptually the same (axis-aligned box) but different dimensions. Separate files seems cleaner given the naming convention.

4. **OBB (oriented bounding box)**: worth including in v1? OBB vs ray and OBB vs OBB are common in 3D engines. Adds SAT tests which are more complex.

5. **Capsule**: useful for character collision. Capsule = two hemispheres + cylinder. Should we include it as a primitive type?
