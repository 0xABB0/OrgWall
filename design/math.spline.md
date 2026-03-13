# math.spline — Spline & Curve Interpolation

## Problem

Melody's curve support is limited to linear, stepped, and a basic bezier (18-sample LUT) in `math.curve.h`. There's no support for hermite, catmull-rom, or any higher-order interpolation. More critically, there's no arc-length parametrization — meaning you can't traverse a curve at constant speed, which is needed for camera paths, object-follow paths, UI animation curves, and smooth timeline scrubbing.

## What This Module Provides

- Standalone interpolation functions: hermite cubic, bezier cubic, catmull-rom
- First and second derivative evaluation for each curve type
- Piecewise curve evaluation over control point arrays (sample at time across segments)
- Arc-length parametrization: build a lookup table (distance -> parameter) for constant-speed traversal
- Frenet frame computation (tangent, normal, binormal) for orienting objects along a path
- Finish modes for looping curves: stop, restart, pingpong

## Inspiration

- **Animation-Engine**: `src/Math/Interpolation/InterpolationFunctions.h` — hermite, bezier, catmull-rom with derivatives
- **Animation-Engine**: `src/Components/PiecewiseCurves/PiecewiseCurve.h` — arc-length table (uniform + adaptive forward differencing), frenet frames, ease-in/out, finish modes

## File Layout

```
math.spline.h         — main interface (interpolation functions, piecewise eval, arc-length)
math.spline.fwd.h     — forward declarations
math.spline.inl        — inline pure math functions (hermite, bezier, catmull-rom, derivatives)
math.spline.c          — piecewise evaluation, arc-length table build/lookup, frenet frame
```

This is separate from `math.curve.h`. The existing curve module is focused on 1D easing curves (bezier LUT for animation easing). Splines are about N-dimensional path interpolation — different domain, different types.

---

## Types

### Core interpolation (all inline, pure math, no allocations)

These operate on `Mel_Vec3` (the primary use case is 3D paths). They're templated-by-hand — we provide `_vec3` variants and can add `_f32`, `_vec2` etc. as needed.

```c
Mel_Vec3 mel_hermite_vec3(Mel_Vec3 p0, Mel_Vec3 t0, Mel_Vec3 p1, Mel_Vec3 t1, f32 t);
Mel_Vec3 mel_hermite_vec3_d1(Mel_Vec3 p0, Mel_Vec3 t0, Mel_Vec3 p1, Mel_Vec3 t1, f32 t);
Mel_Vec3 mel_hermite_vec3_d2(Mel_Vec3 p0, Mel_Vec3 t0, Mel_Vec3 p1, Mel_Vec3 t1, f32 t);

Mel_Vec3 mel_bezier3_vec3(Mel_Vec3 p0, Mel_Vec3 c0, Mel_Vec3 c1, Mel_Vec3 p1, f32 t);
Mel_Vec3 mel_bezier3_vec3_d1(Mel_Vec3 p0, Mel_Vec3 c0, Mel_Vec3 c1, Mel_Vec3 p1, f32 t);
Mel_Vec3 mel_bezier3_vec3_d2(Mel_Vec3 p0, Mel_Vec3 c0, Mel_Vec3 c1, Mel_Vec3 p1, f32 t);

Mel_Vec3 mel_catmull_rom_vec3(Mel_Vec3 p0, Mel_Vec3 p1, Mel_Vec3 p2, Mel_Vec3 p3, f32 t);
Mel_Vec3 mel_catmull_rom_vec3_d1(Mel_Vec3 p0, Mel_Vec3 p1, Mel_Vec3 p2, Mel_Vec3 p3, f32 t);
```

Also f32 variants for 1D curves:

```c
f32 mel_hermite_f32(f32 p0, f32 t0, f32 p1, f32 t1, f32 t);
f32 mel_bezier3_f32(f32 p0, f32 c0, f32 c1, f32 p1, f32 t);
f32 mel_catmull_rom_f32(f32 p0, f32 p1, f32 p2, f32 p3, f32 t);
```

The math is the same across all types — just scalar multiply + add. We do explicit per-type functions instead of C++ templates. Clean, debuggable, no generics headaches.

### Piecewise Curve

A piecewise curve is a sequence of control points evaluated as connected segments. The curve type determines how control points are interpreted.

```c
#define MEL_SPLINE_LINEAR     0
#define MEL_SPLINE_HERMITE    1
#define MEL_SPLINE_CATMULL_ROM 2
#define MEL_SPLINE_BEZIER     3

#define MEL_SPLINE_FINISH_STOP     0
#define MEL_SPLINE_FINISH_RESTART  1
#define MEL_SPLINE_FINISH_PINGPONG 2

typedef struct {
    Mel_Vec3* points;
    u32 point_count;
    u32 spline_type;
} Mel_Spline;
```

For hermite: points are interleaved as `[in_tangent, point, out_tangent]` — 3 Vec3s per key.
For bezier: points are interleaved as `[in_control, point, out_control]` — 3 Vec3s per key.
For catmull-rom: points are just positions — tangents are auto-computed from neighbors.
For linear: points are just positions.

This matches the Animation-Engine's convention and glTF's cubic spline format.

```c
Mel_Vec3 mel_spline_eval(const Mel_Spline* spline, f32 t);
Mel_Vec3 mel_spline_eval_d1(const Mel_Spline* spline, f32 t);
Mel_Vec3 mel_spline_eval_d2(const Mel_Spline* spline, f32 t);
```

`t` ranges from 0 to `point_count - 1` (for linear/catmull-rom) or `(point_count/3) - 1` (for hermite/bezier). The integer part selects the segment, the fractional part is the local parameter.

### Arc-Length Table

```c
typedef struct {
    f32 parameter;
    f32 arc_length;
} Mel_Arc_Length_Entry;

typedef struct {
    Mel_Arc_Length_Entry* entries;
    u32 count;
    f32 total_length;
} Mel_Arc_Length_Table;
```

Build functions:

```c
typedef struct {
    const Mel_Spline* spline;
    u32 samples_per_segment;
    const Mel_Alloc* alloc;
} Mel_Arc_Length_Build_Opt;

void mel_arc_length_build_opt(Mel_Arc_Length_Table* table, Mel_Arc_Length_Build_Opt opt);
#define mel_arc_length_build(table, ...) mel_arc_length_build_opt((table), (Mel_Arc_Length_Build_Opt){__VA_ARGS__})

void mel_arc_length_destroy(Mel_Arc_Length_Table* table, const Mel_Alloc* alloc);
```

Lookup functions:

```c
f32 mel_arc_length_to_param(const Mel_Arc_Length_Table* table, f32 arc_length);
f32 mel_param_to_arc_length(const Mel_Arc_Length_Table* table, f32 param);
```

Both use binary search + linear interpolation between entries. `mel_arc_length_to_param` is the key function — given a distance along the curve, returns the parameter that reaches that distance. This enables constant-speed traversal: `param = mel_arc_length_to_param(table, speed * time)`.

### Frenet Frame

```c
typedef struct {
    Mel_Vec3 tangent;
    Mel_Vec3 normal;
    Mel_Vec3 binormal;
} Mel_Frenet_Frame;

Mel_Frenet_Frame mel_spline_frenet(const Mel_Spline* spline, f32 t);
```

Computed from first and second derivatives. Tangent = normalized d1, binormal = cross(d1, d2), normal = cross(binormal, tangent). Falls back to an arbitrary perpendicular when d2 is zero (straight line segments).

### Path Follower (convenience)

```c
typedef struct {
    const Mel_Spline* spline;
    Mel_Arc_Length_Table arc_table;
    f32 distance_travelled;
    f32 speed;
    f32 total_distance;
    u32 finish_mode;
    i32 direction;
} Mel_Path_Follower;

typedef struct {
    const Mel_Spline* spline;
    f32 speed;
    u32 finish_mode;
    u32 samples_per_segment;
    const Mel_Alloc* alloc;
} Mel_Path_Follower_Init_Opt;

void mel_path_follower_init_opt(Mel_Path_Follower* follower, Mel_Path_Follower_Init_Opt opt);
#define mel_path_follower_init(follower, ...) mel_path_follower_init_opt((follower), (Mel_Path_Follower_Init_Opt){__VA_ARGS__})

void mel_path_follower_destroy(Mel_Path_Follower* follower, const Mel_Alloc* alloc);

typedef struct {
    Mel_Vec3 position;
    Mel_Frenet_Frame frame;
    f32 parameter;
    bool finished;
} Mel_Path_Sample;

Mel_Path_Sample mel_path_follower_advance(Mel_Path_Follower* follower, f32 dt);
void mel_path_follower_reset(Mel_Path_Follower* follower);
```

This wraps spline + arc-length + finish mode into a single "advance by dt, get position + orientation" interface. The common case. Owns the arc-length table.

---

## Integration with anim.pipeline

The existing `anim.pipeline` uses `math.curve` for per-keyframe easing (the bezier LUT). The spline module doesn't replace that — it operates at a different level:

- `math.curve`: 1D easing curve (time remapping within a single keyframe interval)
- `math.spline`: N-dimensional path through space (multi-segment interpolation)

They compose: a keyframe track could use a spline type for value interpolation while using an easing curve for time remapping within each segment. But that's an `anim.pipeline` concern, not a `math.spline` concern.

---

## Design Decisions

**Why separate from math.curve?**
The existing bezier LUT is a 1D timing curve (maps normalized time to normalized output). Splines are spatial paths with derivatives, arc-length, and frenet frames. Different abstraction level, different use cases. Merging them would be confusing.

**Why explicit per-type functions instead of function pointers on Mel_Spline?**
The core interpolation functions (hermite, bezier, catmull-rom) should be callable independently without constructing a Mel_Spline. The Mel_Spline struct adds piecewise evaluation on top. Keeping them separate means the core math is usable anywhere.

**Why uniform forward differencing only (no adaptive)?**
Adaptive builds a better table with fewer samples but adds significant complexity (subdivision stack, tolerance tuning). Uniform with enough samples is simpler and good enough for most cases. We can add adaptive later if needed.

**Why interleaved tangent/control-point storage?**
Matches glTF cubic spline convention. Each key stores `[in, value, out]` contiguously. This is cache-friendly for sequential evaluation and avoids parallel arrays.

---

## Improvements Over Animation-Engine

1. Standalone math functions usable without the piecewise wrapper
2. Explicit derivative functions (d1, d2) instead of derivative order parameter
3. Frenet frame as first-class return type instead of computed inline
4. Path follower convenience struct instead of baking path-following into the curve component
5. No C++ templates — explicit per-type functions

---

## Open Questions for Gabbo

1. **f32 variants of piecewise eval**: do we need `mel_spline_eval_f32` for 1D curves (e.g., animation timing curves), or is Vec3 sufficient for v1?

2. **Quat interpolation**: catmull-rom and hermite don't work directly on quaternions (need squad or similar). Should we handle this in math.spline or leave it to anim.pipeline?

3. **Point ownership**: does `Mel_Spline` own the points array or is it a view? If it's a view, lifetime management is the caller's problem. If it owns, it needs an allocator.

4. **Integration with anim.clip**: should the spline types (hermite, bezier) be available as keyframe interpolation modes in `Mel_Track_Group`? Currently we have easing curves but not spline interpolation between keys.

5. **Path follower scope**: is this too high-level for the math module? Should it live in a `path.*` module instead?
