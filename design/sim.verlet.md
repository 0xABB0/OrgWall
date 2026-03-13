# sim.verlet — Verlet Particle System & Cloth Simulation

## Problem

Melody has no particle physics or soft-body simulation. There's a sim context (`sim.ctx`) for managing fixed-timestep update loops, but no actual physics systems running inside it. Verlet integration is a simple, stable foundation for position-based dynamics — it naturally handles cloth, ropes, soft bodies, and basic particle effects without needing an explicit velocity integration step.

## What This Module Provides

- Verlet particle: position, old_position (velocity is implicit), acceleration, inverse mass, pinned flag
- Particle system: dynamic collection of particles with configurable global forces
- Constraint solver: iterative relaxation loop that enforces constraints after integration
- Built-in constraint types: distance, pin, plane collision, sphere collision
- Verlet integration step: `new_pos = pos + (pos - old_pos) * damping + acceleration * dt^2`
- Cloth grid builder: convenience for generating a particle grid with structural + shear + bend distance constraints
- Per-particle normal recomputation for cloth rendering

## Inspiration

- **Animation-Engine**: `src/Animation/ParticleSimulations/VerletParticleSystem.h/.cpp`
- **Animation-Engine**: `src/Animation/ParticleSimulations/IConstraint.h`
- **Animation-Engine**: `src/Components/Particles/Cloth.h/.cpp`, `ClothConstraints.h/.cpp`
- General reference: Jakobsen's "Advanced Character Physics" (GDC 2001)

## File Layout

```
sim.verlet.h          — main interface (particle, system, constraint types, API)
sim.verlet.fwd.h      — forward declarations
sim.verlet.inl         — inline definitions
sim.verlet.c           — integration, constraint solving, force application
sim.verlet.cloth.c     — cloth grid builder, normal computation, index generation
```

---

## Types

### Mel_Verlet_Particle

```c
typedef struct {
    Mel_Vec3 pos;
    Mel_Vec3 old_pos;
    Mel_Vec3 accel;
    Mel_Vec3 normal;
    Mel_Vec2 uv;
    f32 inv_mass;
} Mel_Verlet_Particle;
```

`inv_mass == 0` means pinned (infinite mass, cannot move). This replaces both the `canMove` bool and a separate pin concept — it's one mechanism. The normal and uv are stored per-particle for cloth rendering convenience. For non-cloth use cases they're just ignored.

No `old_pos` initialization trap: `mel_verlet_add_particle` sets `old_pos = pos` so there's no accidental velocity on frame 1.

### Mel_Verlet_System

```c
typedef struct {
    Mel_Verlet_Particle* particles;
    u32 count;
    u32 capacity;

    Mel_Verlet_Constraint* constraints;
    u32 constraint_count;
    u32 constraint_capacity;

    Mel_Verlet_Collider* colliders;
    u32 collider_count;
    u32 collider_capacity;

    f32 damping;
    u32 solver_iterations;

    const Mel_Alloc* alloc;
} Mel_Verlet_System;
```

Dynamic arrays for both particles and constraints (no MAX_PARTICLES). `damping` is the velocity retention factor (0.997 is typical). `solver_iterations` controls constraint stiffness (more iterations = stiffer, typically 3-8 for cloth).

### Mel_Verlet_Constraint

Constraints use a tagged union with a function pointer for the solve step. This avoids enums (no `CONSTRAINT_TYPE_DISTANCE` enum) while keeping things extensible — you can register custom constraint types.

```c
typedef void (*Mel_Verlet_Constraint_Solve_Fn)(
    Mel_Verlet_System* sys,
    void* data
);

typedef struct {
    Mel_Verlet_Constraint_Solve_Fn solve;
    union {
        Mel_Verlet_Distance_Data distance;
        Mel_Verlet_Pin_Data pin;
        Mel_Verlet_Plane_Data plane;
        Mel_Verlet_Sphere_Data sphere;
        void* custom;
    };
} Mel_Verlet_Constraint;
```

### Built-in constraint data

```c
typedef struct {
    u32 p0;
    u32 p1;
    f32 rest_length;
} Mel_Verlet_Distance_Data;

typedef struct {
    u32 particle;
    Mel_Vec3 position;
} Mel_Verlet_Pin_Data;

typedef struct {
    Mel_Plane plane;
} Mel_Verlet_Plane_Data;

typedef struct {
    Mel_Sphere sphere;
} Mel_Verlet_Sphere_Data;
```

### Mel_Verlet_Collider

```c
#define MEL_VERLET_COLLIDER_SPHERE 0
#define MEL_VERLET_COLLIDER_PLANE  1

typedef struct {
    u32 type;
    union {
        Mel_Sphere sphere;
        Mel_Plane plane;
    };
} Mel_Verlet_Collider;
```

Colliders use a simple type tag + union. There are only 2 types and they're internal to the verlet system, so a tag is acceptable here (these aren't API-level enums — they're internal dispatch for the collision pass).

Distance constraints use `inv_mass` for the split: if both particles are movable, each moves proportionally to its inverse mass ratio. If one is pinned (`inv_mass == 0`), only the other moves. This is more physically correct than the Animation-Engine's equal 0.5/0.5 split.

---

## API

### System lifecycle

```c
typedef struct {
    u32 initial_capacity;
    u32 initial_constraint_capacity;
    f32 damping;
    u32 solver_iterations;
    const Mel_Alloc* alloc;
} Mel_Verlet_Init_Opt;

void mel_verlet_init_opt(Mel_Verlet_System* sys, Mel_Verlet_Init_Opt opt);
#define mel_verlet_init(sys, ...) mel_verlet_init_opt((sys), (Mel_Verlet_Init_Opt){__VA_ARGS__})

void mel_verlet_shutdown(Mel_Verlet_System* sys);
```

### Particles

```c
typedef struct {
    Mel_Vec3 pos;
    f32 inv_mass;
    Mel_Vec2 uv;
} Mel_Verlet_Add_Particle_Opt;

u32 mel_verlet_add_particle_opt(Mel_Verlet_System* sys, Mel_Verlet_Add_Particle_Opt opt);
#define mel_verlet_add_particle(sys, ...) mel_verlet_add_particle_opt((sys), (Mel_Verlet_Add_Particle_Opt){__VA_ARGS__})
```

Returns the particle index. Sets `old_pos = pos`, `accel = {0}`, `normal = {0,1,0}`. Default `inv_mass` is `1.0` if not specified (zero-init means pinned, so we need the _opt pattern to set a sensible default — this is a design decision to discuss).

```c
void mel_verlet_pin(Mel_Verlet_System* sys, u32 particle);
void mel_verlet_unpin(Mel_Verlet_System* sys, u32 particle, f32 inv_mass);
```

### Forces

```c
void mel_verlet_apply_force(Mel_Verlet_System* sys, Mel_Vec3 force);
void mel_verlet_apply_force_at(Mel_Verlet_System* sys, u32 particle, Mel_Vec3 force);
void mel_verlet_apply_wind(Mel_Verlet_System* sys, Mel_Vec3 wind);
```

`apply_force` adds to ALL particles' acceleration. `apply_force_at` targets one. `apply_wind` uses the dot-product-with-normal model from the Animation-Engine (scales force by how much the particle faces the wind direction).

### Constraints

```c
void mel_verlet_add_distance(Mel_Verlet_System* sys, u32 p0, u32 p1, f32 rest_length);
void mel_verlet_add_pin(Mel_Verlet_System* sys, u32 particle, Mel_Vec3 position);
void mel_verlet_add_custom(Mel_Verlet_System* sys, Mel_Verlet_Constraint_Solve_Fn fn, void* data);

void mel_verlet_remove_constraint(Mel_Verlet_System* sys, u32 index);
```

Each `add_*` pushes a constraint into the dynamic array with the appropriate solve function pre-filled. `remove` uses swap-remove (moves the last constraint into the removed slot).

For distance: if `rest_length` is 0 (or negative), it auto-computes from current particle positions. This is the common case when building cloth grids.

Collision (planes, spheres) is NOT a constraint — it's a global pass. See `mel_verlet_step`.

### Simulation step

```c
void mel_verlet_step(Mel_Verlet_System* sys, f32 dt);
```

This is the main update. Order:
1. Verlet integration (skip particles with inv_mass == 0)
2. Constraint solving (N iterations over all constraints)
3. Collision pass (test all particles against all registered colliders)
4. Clear accelerations

### Collider registration

Colliders are NOT constraints. They're geometry that all particles are tested against after constraint solving.

```c
u32 mel_verlet_add_collider_sphere(Mel_Verlet_System* sys, Mel_Sphere sphere);
u32 mel_verlet_add_collider_plane(Mel_Verlet_System* sys, Mel_Plane plane);
void mel_verlet_remove_collider(Mel_Verlet_System* sys, u32 index);
void mel_verlet_update_collider_sphere(Mel_Verlet_System* sys, u32 index, Mel_Sphere sphere);
```

Colliders are stored in a separate dynamic array. Each frame, every particle is tested against every collider. For the expected collider counts (handful of spheres/planes), this is fine.

`update_collider_sphere` lets you move colliders at runtime (e.g., a sphere the cloth drapes over).

Designed to be called from a `Mel_Sim_Fixed` update callback with a fixed dt.

### Cloth helpers

```c
typedef struct {
    u32 width;
    u32 height;
    f32 spacing;
    Mel_Vec3 origin;
    bool pin_top_left;
    bool pin_top_right;
    bool pin_top_row;
    bool add_shear;
    bool add_bend;
} Mel_Verlet_Cloth_Opt;

void mel_verlet_cloth_opt(Mel_Verlet_System* sys, Mel_Verlet_Cloth_Opt opt);
#define mel_verlet_cloth(sys, ...) mel_verlet_cloth_opt((sys), (Mel_Verlet_Cloth_Opt){__VA_ARGS__})
```

Populates the system with a `width * height` particle grid and adds constraints:
- **Structural**: horizontal + vertical neighbors (rest length = spacing)
- **Shear** (optional): diagonal neighbors (rest length = spacing * sqrt(2))
- **Bend** (optional): skip-one neighbors horizontally and vertically (rest length = spacing * 2)

Shear constraints prevent the grid from collapsing diagonally. Bend constraints resist folding. The Animation-Engine only had structural — we can do better.

Also sets up UVs automatically (normalized 0-1 across the grid).

### Normal computation (for cloth rendering)

```c
void mel_verlet_compute_normals(Mel_Verlet_System* sys, u32 width, u32 height);
```

Computes averaged face normals for a grid-layout system. Takes width/height because the system itself doesn't know its topology — it's just particles and constraints. This follows the Animation-Engine's 4-quadrant averaging approach but stores results directly in `particle.normal`.

### Mesh output (for rendering)

```c
typedef struct {
    Mel_Vec3* out_positions;
    Mel_Vec3* out_normals;
    Mel_Vec4* out_colors;
    u32* out_indices;
    u32 vertex_count;
    u32 index_count;
} Mel_Verlet_Mesh_Output;

Mel_Verlet_Mesh_Output mel_verlet_cloth_mesh(Mel_Verlet_System* sys, u32 width, u32 height, Mel_Vec4 color);
```

Fills caller-provided buffers with mesh data ready for `mel_draw_mesh`. Generates triangle indices for the grid. Returns counts so the caller can build a `Mel_Mesh` and push it to a render list.

This keeps the verlet system decoupled from rendering — the system doesn't own GPU buffers. The caller (example, demo, game code) decides how to render.

---

## Integration with Mel_Sim_Ctx

Typical usage:

```c
Mel_Sim_Fixed* physics = mel_sim_add_fixed(sim, .fixed_dt = 1.0f / 60.0f);
mel_sim_fixed_add_update(physics, cloth_update, .user = &my_cloth_system);

void cloth_update(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Mel_Verlet_System* sys = user;
    mel_verlet_apply_force(sys, (Mel_Vec3){0, -9.8f, 0});
    mel_verlet_apply_wind(sys, (Mel_Vec3){0, 0, 2.0f});
    mel_verlet_step(sys, dt);
    mel_verlet_compute_normals(sys, cloth_width, cloth_height);
}
```

The verlet system itself doesn't know about `Mel_Sim_Ctx`. It's just a data structure + step function. The sim context drives it.

---

## Design Decisions

**Why inv_mass instead of a bool pin flag?**
- One field serves two purposes: pinning (inv_mass=0) and mass-weighted constraint resolution
- More physically correct distance constraint solving (heavier particles move less)
- No wasted bool in the struct

**Why function pointer per constraint instead of a type tag + switch?**
- Extensible without modifying the constraint struct
- No enum needed (MEL-STYLE-001)
- Custom constraints are first-class — same dispatch path as built-in ones
- The overhead of an indirect call per constraint per iteration is negligible compared to the math

**Why the system doesn't own GPU resources?**
- Keeps sim.verlet as pure simulation, no rendering dependency
- Different users render differently (mesh pass, debug lines, point sprites)
- Follows existing Melody patterns where sim and render are separate concerns

**Why cloth helpers are in a separate .c file?**
- Grid topology is a convenience layer, not core verlet
- Keeps sim.verlet.c focused on integration + constraint solving

**Why no SoA?**
- Starting with AoS (array of Mel_Verlet_Particle) for simplicity
- Expected particle counts for cloth are modest (hundreds to low thousands)
- If we later need SIMD perf for massive particle counts, we can refactor to SoA — but that's premature for v1

---

## Improvements Over Animation-Engine

1. **Dynamic particle/constraint arrays** instead of `MAX_PARTICLES = 1024`
2. **Mass-weighted constraint resolution** instead of equal 0.5/0.5 split
3. **Shear + bend constraints** for cloth (they only had structural)
4. **Function pointer constraints** instead of C++ virtual dispatch
5. **Decoupled from rendering** — no OpenGL calls in the sim
6. **No singleton manager** — just a data struct you own
7. **Plane collision constraint** in addition to sphere

---

## Resolved Decisions

1. **Default inv_mass = 1.0** (movable). The _opt struct sets `.inv_mass = 1.0f` as default. To pin a particle, explicitly pass `.inv_mass = 0`.

2. **Constraint removal**: yes, included. Swap-remove approach (last constraint fills the removed slot).

3. **Collision as global pass**: not per-particle constraints. After verlet integration and distance constraint solving, run collision against geometry (spheres, planes) for ALL particles. Uses `Mel_Sphere` and `Mel_Plane` from `math.geo`.

4. **Wind clamping**: `max(0, dot(normal, wind_dir))` — wind only pushes surfaces facing it. Physically correct.

5. **Mesh output helper**: in scope for v1. Cloth grid produces mesh data compatible with `mel_draw_mesh`.

## Dependencies

- `math.geo.sphere` (`Mel_Sphere`) — for sphere collision
- `math.geo.aabb` (`Mel_AABB`) — for AABB collision (future)
- `math.geo.plane` (`Mel_Plane`) — already exists, for plane collision
- `math.geo.intersect` — point-vs-sphere, point-vs-plane distance
