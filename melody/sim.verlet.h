#pragma once

#include "core.types.h"
#include "sim.verlet.fwd.h"
#include "math.vec2.h"
#include "math.vec3.h"
#include "math.geo.sphere.h"
#include "math.geo.plane.h"
#include "allocator.fwd.h"

typedef struct {
    Mel_Vec3 pos;
    Mel_Vec3 old_pos;
    Mel_Vec3 accel;
    Mel_Vec3 normal;
    Mel_Vec2 uv;
    f32 inv_mass;
} Mel_Verlet_Particle;

typedef void (*Mel_Verlet_Constraint_Solve_Fn)(
    Mel_Verlet_System* sys,
    void* data);

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
    Mel_Verlet_Constraint_Solve_Fn solve;
    union {
        Mel_Verlet_Distance_Data distance;
        Mel_Verlet_Pin_Data pin;
        void* custom;
    };
} Mel_Verlet_Constraint;

#define MEL_VERLET_COLLIDER_SPHERE 0
#define MEL_VERLET_COLLIDER_PLANE  1

typedef struct {
    u32 type;
    union {
        Mel_Sphere sphere;
        Mel_Plane plane;
    };
} Mel_Verlet_Collider;

struct Mel_Verlet_System {
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
};

typedef struct {
    u32 initial_capacity;
    u32 initial_constraint_capacity;
    f32 damping;
    u32 solver_iterations;
    const Mel_Alloc* alloc;
} Mel_Verlet_Init_Opt;

void mel_verlet_init_opt(Mel_Verlet_System* sys, Mel_Verlet_Init_Opt opt);
#define mel_verlet_init(sys, ...) mel_verlet_init_opt((sys), (Mel_Verlet_Init_Opt){ .damping = 0.997f, .solver_iterations = 4, __VA_ARGS__ })

void mel_verlet_shutdown(Mel_Verlet_System* sys);

typedef struct {
    Mel_Vec3 pos;
    f32 inv_mass;
    Mel_Vec2 uv;
} Mel_Verlet_Add_Particle_Opt;

u32 mel_verlet_add_particle_opt(Mel_Verlet_System* sys, Mel_Verlet_Add_Particle_Opt opt);
#define mel_verlet_add_particle(sys, ...) mel_verlet_add_particle_opt((sys), (Mel_Verlet_Add_Particle_Opt){ .inv_mass = 1.0f, __VA_ARGS__ })

void mel_verlet_pin(Mel_Verlet_System* sys, u32 particle);
void mel_verlet_unpin(Mel_Verlet_System* sys, u32 particle, f32 inv_mass);

void mel_verlet_apply_force(Mel_Verlet_System* sys, Mel_Vec3 force);
void mel_verlet_apply_force_at(Mel_Verlet_System* sys, u32 particle, Mel_Vec3 force);
void mel_verlet_apply_wind(Mel_Verlet_System* sys, Mel_Vec3 wind);

void mel_verlet_add_distance(Mel_Verlet_System* sys, u32 p0, u32 p1, f32 rest_length);
void mel_verlet_add_pin(Mel_Verlet_System* sys, u32 particle, Mel_Vec3 position);
void mel_verlet_add_custom(Mel_Verlet_System* sys, Mel_Verlet_Constraint_Solve_Fn fn, void* data);
void mel_verlet_remove_constraint(Mel_Verlet_System* sys, u32 index);

u32 mel_verlet_add_collider_sphere(Mel_Verlet_System* sys, Mel_Sphere sphere);
u32 mel_verlet_add_collider_plane(Mel_Verlet_System* sys, Mel_Plane plane);
void mel_verlet_remove_collider(Mel_Verlet_System* sys, u32 index);
void mel_verlet_update_collider_sphere(Mel_Verlet_System* sys, u32 index, Mel_Sphere sphere);

void mel_verlet_step(Mel_Verlet_System* sys, f32 dt);

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
#define mel_verlet_cloth(sys, ...) mel_verlet_cloth_opt((sys), (Mel_Verlet_Cloth_Opt){ .spacing = 0.2f, .pin_top_left = true, .pin_top_right = true, .add_shear = true, .add_bend = true, __VA_ARGS__ })

void mel_verlet_compute_normals(Mel_Verlet_System* sys, u32 width, u32 height);

typedef struct {
    u32 vertex_count;
    u32 index_count;
} Mel_Verlet_Mesh_Counts;

Mel_Verlet_Mesh_Counts mel_verlet_cloth_mesh_counts(u32 width, u32 height);

void mel_verlet_cloth_mesh(
    Mel_Verlet_System* sys,
    u32 width, u32 height,
    Mel_Vec3* out_positions,
    Mel_Vec3* out_normals,
    Mel_Vec4* out_colors,
    u32* out_indices,
    Mel_Vec4 color);
