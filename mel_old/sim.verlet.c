#include "sim.verlet.h"
#include "allocator.h"
#include "math.vec4.h"
#include <assert.h>
#include <math.h>

static void mel__verlet_grow_particles(Mel_Verlet_System* sys)
{
    u32 new_cap = sys->capacity ? sys->capacity * 2 : 64;
    if (sys->particles)
        sys->particles = (Mel_Verlet_Particle*)mel_realloc(
            sys->alloc, sys->particles,
            sizeof(Mel_Verlet_Particle) * new_cap);
    else
        sys->particles = (Mel_Verlet_Particle*)mel_alloc(
            sys->alloc, sizeof(Mel_Verlet_Particle) * new_cap);
    sys->capacity = new_cap;
}

static void mel__verlet_grow_constraints(Mel_Verlet_System* sys)
{
    u32 new_cap = sys->constraint_capacity ? sys->constraint_capacity * 2 : 64;
    if (sys->constraints)
        sys->constraints = (Mel_Verlet_Constraint*)mel_realloc(
            sys->alloc, sys->constraints,
            sizeof(Mel_Verlet_Constraint) * new_cap);
    else
        sys->constraints = (Mel_Verlet_Constraint*)mel_alloc(
            sys->alloc, sizeof(Mel_Verlet_Constraint) * new_cap);
    sys->constraint_capacity = new_cap;
}

void mel_verlet_init_opt(Mel_Verlet_System* sys, Mel_Verlet_Init_Opt opt)
{
    assert(sys);
    *sys = (Mel_Verlet_System){
        .damping = opt.damping > 0.0f ? opt.damping : 0.997f,
        .solver_iterations = opt.solver_iterations > 0 ? opt.solver_iterations : 8,
        .alloc = opt.alloc,
    };

    if (opt.initial_capacity > 0) {
        sys->particles = (Mel_Verlet_Particle*)mel_alloc(
            sys->alloc, sizeof(Mel_Verlet_Particle) * opt.initial_capacity);
        sys->capacity = opt.initial_capacity;
    }

    u32 cc = opt.initial_constraint_capacity;
    if (cc > 0) {
        sys->constraints = (Mel_Verlet_Constraint*)mel_alloc(
            sys->alloc, sizeof(Mel_Verlet_Constraint) * cc);
        sys->constraint_capacity = cc;
    }
}

void mel_verlet_shutdown(Mel_Verlet_System* sys)
{
    assert(sys);
    if (sys->particles)
        mel_dealloc(sys->alloc, sys->particles);
    if (sys->constraints)
        mel_dealloc(sys->alloc, sys->constraints);
    *sys = (Mel_Verlet_System){0};
}

u32 mel_verlet_add_particle_opt(Mel_Verlet_System* sys, Mel_Verlet_Add_Particle_Opt opt)
{
    assert(sys);
    if (sys->count >= sys->capacity)
        mel__verlet_grow_particles(sys);

    u32 idx = sys->count++;
    sys->particles[idx] = (Mel_Verlet_Particle){
        .pos = opt.pos,
        .old_pos = opt.pos,
        .accel = MEL_VEC3_ZERO,
        .normal = MEL_VEC3_UP,
        .uv = opt.uv,
        .inv_mass = opt.inv_mass,
    };
    return idx;
}

void mel_verlet_pin(Mel_Verlet_System* sys, u32 particle)
{
    assert(sys && particle < sys->count);
    sys->particles[particle].inv_mass = 0;
}

void mel_verlet_unpin(Mel_Verlet_System* sys, u32 particle, f32 inv_mass)
{
    assert(sys && particle < sys->count);
    assert(inv_mass > 0);
    sys->particles[particle].inv_mass = inv_mass;
}

void mel_verlet_apply_force(Mel_Verlet_System* sys, Mel_Vec3 force)
{
    assert(sys);
    for (u32 i = 0; i < sys->count; i++)
        sys->particles[i].accel = mel_vec3_add(sys->particles[i].accel, force);
}

void mel_verlet_apply_force_at(Mel_Verlet_System* sys, u32 particle, Mel_Vec3 force)
{
    assert(sys && particle < sys->count);
    sys->particles[particle].accel = mel_vec3_add(sys->particles[particle].accel, force);
}

void mel_verlet_apply_wind(Mel_Verlet_System* sys, Mel_Vec3 wind)
{
    assert(sys);
    f32 wind_len = mel_vec3_len(wind);
    if (wind_len < 1e-7f) return;
    Mel_Vec3 wind_dir = mel_vec3_scale(wind, 1.0f / wind_len);

    for (u32 i = 0; i < sys->count; i++) {
        f32 factor = mel_vec3_dot(sys->particles[i].normal, wind_dir);
        if (factor <= 0) continue;
        Mel_Vec3 wind_force = mel_vec3_scale(sys->particles[i].normal, wind_len * factor);
        sys->particles[i].accel = mel_vec3_add(sys->particles[i].accel, wind_force);
    }
}

static void mel__solve_distance(Mel_Verlet_System* sys, void* data)
{
    Mel_Verlet_Distance_Data* d = (Mel_Verlet_Distance_Data*)data;
    Mel_Verlet_Particle* p0 = &sys->particles[d->p0];
    Mel_Verlet_Particle* p1 = &sys->particles[d->p1];

    Mel_Vec3 delta = mel_vec3_sub(p1->pos, p0->pos);
    f32 dist = mel_vec3_len(delta);
    if (dist < 1e-7f) return;

    f32 diff = (dist - d->rest_length) / dist;
    Mel_Vec3 correction = mel_vec3_scale(delta, diff);

    f32 w0 = p0->inv_mass;
    f32 w1 = p1->inv_mass;
    f32 w_total = w0 + w1;
    if (w_total < 1e-7f) return;

    if (w0 > 0)
        p0->pos = mel_vec3_add(p0->pos, mel_vec3_scale(correction, w0 / w_total));
    if (w1 > 0)
        p1->pos = mel_vec3_sub(p1->pos, mel_vec3_scale(correction, w1 / w_total));
}

static void mel__solve_pin(Mel_Verlet_System* sys, void* data)
{
    Mel_Verlet_Pin_Data* d = (Mel_Verlet_Pin_Data*)data;
    sys->particles[d->particle].pos = d->position;
}

void mel_verlet_add_distance(Mel_Verlet_System* sys, u32 p0, u32 p1, f32 rest_length)
{
    assert(sys && p0 < sys->count && p1 < sys->count);
    if (sys->constraint_count >= sys->constraint_capacity)
        mel__verlet_grow_constraints(sys);

    if (rest_length <= 0)
        rest_length = mel_vec3_dist(sys->particles[p0].pos, sys->particles[p1].pos);

    Mel_Verlet_Constraint* c = &sys->constraints[sys->constraint_count++];
    c->solve = mel__solve_distance;
    c->distance = (Mel_Verlet_Distance_Data){ .p0 = p0, .p1 = p1, .rest_length = rest_length };
}

void mel_verlet_add_pin(Mel_Verlet_System* sys, u32 particle, Mel_Vec3 position)
{
    assert(sys && particle < sys->count);
    if (sys->constraint_count >= sys->constraint_capacity)
        mel__verlet_grow_constraints(sys);

    Mel_Verlet_Constraint* c = &sys->constraints[sys->constraint_count++];
    c->solve = mel__solve_pin;
    c->pin = (Mel_Verlet_Pin_Data){ .particle = particle, .position = position };
}

void mel_verlet_add_custom(Mel_Verlet_System* sys, Mel_Verlet_Constraint_Solve_Fn fn, void* data)
{
    assert(sys && fn);
    if (sys->constraint_count >= sys->constraint_capacity)
        mel__verlet_grow_constraints(sys);

    Mel_Verlet_Constraint* c = &sys->constraints[sys->constraint_count++];
    c->solve = fn;
    c->custom = data;
}

void mel_verlet_remove_constraint(Mel_Verlet_System* sys, u32 index)
{
    assert(sys && index < sys->constraint_count);
    sys->constraints[index] = sys->constraints[--sys->constraint_count];
}

static void mel__solve_sphere(Mel_Verlet_System* sys, void* data)
{
    Mel_Sphere* s = (Mel_Sphere*)data;
    for (u32 i = 0; i < sys->count; i++) {
        Mel_Verlet_Particle* p = &sys->particles[i];
        if (p->inv_mass == 0) continue;

        Mel_Vec3 diff = mel_vec3_sub(p->pos, s->center);
        f32 dist_sq = mel_vec3_len_sq(diff);
        if (dist_sq >= s->radius * s->radius) continue;

        f32 dist = sqrtf(dist_sq);
        Mel_Vec3 push_dir;
        if (dist < 1e-7f)
            push_dir = MEL_VEC3_UP;
        else
            push_dir = mel_vec3_scale(diff, 1.0f / dist);
        p->pos = mel_vec3_add(s->center, mel_vec3_scale(push_dir, s->radius));
    }
}

static void mel__solve_plane(Mel_Verlet_System* sys, void* data)
{
    Mel_Plane* pl = (Mel_Plane*)data;
    Mel_Vec3 n = mel_plane_normal(*pl);
    for (u32 i = 0; i < sys->count; i++) {
        Mel_Verlet_Particle* p = &sys->particles[i];
        if (p->inv_mass == 0) continue;

        f32 d = mel_plane_dist_to_point(*pl, p->pos);
        if (d < 0)
            p->pos = mel_vec3_add(p->pos, mel_vec3_scale(n, -d));
    }
}

u32 mel_verlet_add_collider_sphere(Mel_Verlet_System* sys, Mel_Sphere sphere)
{
    assert(sys);
    if (sys->constraint_count >= sys->constraint_capacity)
        mel__verlet_grow_constraints(sys);

    u32 idx = sys->constraint_count++;
    Mel_Verlet_Constraint* c = &sys->constraints[idx];
    c->solve = mel__solve_sphere;
    c->sphere = sphere;
    return idx;
}

u32 mel_verlet_add_collider_plane(Mel_Verlet_System* sys, Mel_Plane plane)
{
    assert(sys);
    if (sys->constraint_count >= sys->constraint_capacity)
        mel__verlet_grow_constraints(sys);

    u32 idx = sys->constraint_count++;
    Mel_Verlet_Constraint* c = &sys->constraints[idx];
    c->solve = mel__solve_plane;
    c->plane = plane;
    return idx;
}

void mel_verlet_remove_collider(Mel_Verlet_System* sys, u32 index)
{
    mel_verlet_remove_constraint(sys, index);
}

void mel_verlet_update_collider_sphere(Mel_Verlet_System* sys, u32 index, Mel_Sphere sphere)
{
    assert(sys && index < sys->constraint_count);
    assert(sys->constraints[index].solve == mel__solve_sphere);
    sys->constraints[index].sphere = sphere;
}

static void mel__verlet_integrate(Mel_Verlet_System* sys, f32 dt)
{
    f32 dt2 = dt * dt;
    f32 damping = powf(sys->damping, dt * 60.0f);
    for (u32 i = 0; i < sys->count; i++) {
        Mel_Verlet_Particle* p = &sys->particles[i];
        if (p->inv_mass == 0) continue;

        Mel_Vec3 vel = mel_vec3_scale(mel_vec3_sub(p->pos, p->old_pos), damping);
        Mel_Vec3 accel_step = mel_vec3_scale(p->accel, dt2);

        p->old_pos = p->pos;
        p->pos = mel_vec3_add(mel_vec3_add(p->pos, vel), accel_step);
    }
}

static void mel__verlet_solve_constraints(Mel_Verlet_System* sys)
{
    for (u32 iter = 0; iter < sys->solver_iterations; iter++) {
        for (u32 i = 0; i < sys->constraint_count; i++) {
            Mel_Verlet_Constraint* c = &sys->constraints[i];
            c->solve(sys, &c->distance);
        }
    }
}

static void mel__verlet_clear_forces(Mel_Verlet_System* sys)
{
    for (u32 i = 0; i < sys->count; i++)
        sys->particles[i].accel = MEL_VEC3_ZERO;
}

void mel_verlet_step(Mel_Verlet_System* sys, f32 dt)
{
    assert(sys);
    mel__verlet_integrate(sys, dt);
    mel__verlet_solve_constraints(sys);
    mel__verlet_clear_forces(sys);
}
