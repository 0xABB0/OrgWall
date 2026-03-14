#include "../melody/test.harness.h"
#include "../melody/sim.verlet.h"
#include "../melody/allocator.heap.h"
#include "../melody/math.vec4.h"
#include <math.h>

MEL_TEST(verlet_init_shutdown, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(sys.count, 0);
    MEL_ASSERT_EQ(sys.constraint_count, 0);
    MEL_ASSERT_EQ(sys.constraint_count, 0);
    MEL_ASSERT_FLOAT_EQ(sys.damping, 0.997f, 1e-6f);
    MEL_ASSERT_EQ(sys.solver_iterations, 8);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_add_particle, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    u32 p = mel_verlet_add_particle(&sys, .pos = mel_vec3(1, 2, 3));
    MEL_ASSERT_EQ(p, 0);
    MEL_ASSERT_EQ(sys.count, 1);
    MEL_ASSERT_FLOAT_EQ(sys.particles[0].pos.x, 1.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(sys.particles[0].pos.y, 2.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(sys.particles[0].pos.z, 3.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(sys.particles[0].inv_mass, 1.0f, 1e-6f);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_pin_particle, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    u32 p = mel_verlet_add_particle(&sys, .pos = mel_vec3(0, 5, 0));
    mel_verlet_pin(&sys, p);
    MEL_ASSERT_FLOAT_EQ(sys.particles[p].inv_mass, 0.0f, 1e-6f);

    mel_verlet_apply_force(&sys, mel_vec3(0, -9.8f, 0));
    mel_verlet_step(&sys, 1.0f / 60.0f);

    MEL_ASSERT_FLOAT_EQ(sys.particles[p].pos.x, 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(sys.particles[p].pos.y, 5.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(sys.particles[p].pos.z, 0.0f, 1e-6f);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_gravity_drops_particle, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    mel_verlet_add_particle(&sys, .pos = mel_vec3(0, 10, 0));
    f32 start_y = sys.particles[0].pos.y;

    for (int i = 0; i < 60; i++) {
        mel_verlet_apply_force(&sys, mel_vec3(0, -9.8f, 0));
        mel_verlet_step(&sys, 1.0f / 60.0f);
    }

    MEL_ASSERT_LT(sys.particles[0].pos.y, start_y);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_distance_constraint, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .solver_iterations = 10, .alloc = mel_alloc_heap());

    u32 a = mel_verlet_add_particle(&sys, .pos = mel_vec3(0, 0, 0));
    u32 b = mel_verlet_add_particle(&sys, .pos = mel_vec3(1, 0, 0));
    mel_verlet_pin(&sys, a);
    mel_verlet_add_distance(&sys, a, b, 0);

    for (int i = 0; i < 120; i++) {
        mel_verlet_apply_force(&sys, mel_vec3(0, -9.8f, 0));
        mel_verlet_step(&sys, 1.0f / 60.0f);
    }

    f32 dist = mel_vec3_dist(sys.particles[a].pos, sys.particles[b].pos);
    MEL_ASSERT_FLOAT_EQ(dist, 1.0f, 0.05f);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_plane_collision, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    mel_verlet_add_particle(&sys, .pos = mel_vec3(0, 5, 0));
    mel_verlet_add_collider_plane(&sys,
        mel_plane_from_normal_point(mel_vec3(0, 1, 0), mel_vec3(0, 0, 0)));

    for (int i = 0; i < 300; i++) {
        mel_verlet_apply_force(&sys, mel_vec3(0, -9.8f, 0));
        mel_verlet_step(&sys, 1.0f / 60.0f);
    }

    MEL_ASSERT_GE(sys.particles[0].pos.y, -0.01f);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_sphere_collision, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    mel_verlet_add_particle(&sys, .pos = mel_vec3(0, 5, 0));
    mel_verlet_add_collider_sphere(&sys, mel_sphere(mel_vec3(0, 0, 0), 2.0f));

    for (int i = 0; i < 300; i++) {
        mel_verlet_apply_force(&sys, mel_vec3(0, -9.8f, 0));
        mel_verlet_step(&sys, 1.0f / 60.0f);
    }

    f32 dist = mel_vec3_dist(sys.particles[0].pos, mel_vec3(0, 0, 0));
    MEL_ASSERT_GE(dist, 1.99f);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_cloth_creation, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    mel_verlet_cloth(&sys, .width = 8, .height = 6, .origin = mel_vec3(0, 5, 0),
        .pin_top_left = true, .pin_top_right = true, .add_shear = true, .add_bend = true);

    MEL_ASSERT_EQ(sys.count, 48);
    MEL_ASSERT_GT(sys.constraint_count, 0u);

    MEL_ASSERT_FLOAT_EQ(sys.particles[0].inv_mass, 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(sys.particles[7].inv_mass, 0.0f, 1e-6f);

    MEL_ASSERT_FLOAT_EQ(sys.particles[8].inv_mass, 1.0f, 1e-6f);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_cloth_mesh_output, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    u32 w = 4, h = 4;
    mel_verlet_cloth(&sys, .width = w, .height = h,
        .pin_top_left = true, .pin_top_right = true, .add_shear = true, .add_bend = true);

    Mel_Verlet_Mesh_Counts counts = mel_verlet_cloth_mesh_counts(w, h);
    MEL_ASSERT_EQ(counts.vertex_count, 16);
    MEL_ASSERT_EQ(counts.index_count, 54);

    Mel_Vec3 positions[16];
    Mel_Vec3 normals[16];
    Mel_Vec2 uvs[16];
    Mel_Vec4 tangents[16];
    Mel_Vec4 colors[16];
    u32 indices[54];

    mel_verlet_compute_normals(&sys, w, h);
    mel_verlet_compute_tangents(&sys, w, h, tangents);
    mel_verlet_cloth_mesh(&sys, w, h, positions, normals, uvs, colors, indices,
        (Mel_Vec4){{1, 1, 1, 1}});

    for (u32 i = 0; i < 54; i++)
        MEL_ASSERT_LT(indices[i], 16u);

    MEL_ASSERT_FLOAT_EQ(uvs[0].x, 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(uvs[0].y, 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(uvs[3].x, 1.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(uvs[3].y, 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(uvs[15].x, 1.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(uvs[15].y, 1.0f, 1e-6f);

    f32 t_len = sqrtf(tangents[0].x * tangents[0].x +
                      tangents[0].y * tangents[0].y +
                      tangents[0].z * tangents[0].z);
    MEL_ASSERT_FLOAT_EQ(t_len, 1.0f, 1e-5f);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_remove_constraint, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    mel_verlet_add_particle(&sys, .pos = mel_vec3(0, 0, 0));
    mel_verlet_add_particle(&sys, .pos = mel_vec3(1, 0, 0));
    mel_verlet_add_particle(&sys, .pos = mel_vec3(2, 0, 0));

    mel_verlet_add_distance(&sys, 0, 1, 0);
    mel_verlet_add_distance(&sys, 1, 2, 0);

    MEL_ASSERT_EQ(sys.constraint_count, 2);
    mel_verlet_remove_constraint(&sys, 0);
    MEL_ASSERT_EQ(sys.constraint_count, 1);

    mel_verlet_shutdown(&sys);
}

MEL_TEST(verlet_wind_facing, .tags = "sim")
{
    Mel_Verlet_System sys;
    mel_verlet_init(&sys, .alloc = mel_alloc_heap());

    mel_verlet_add_particle(&sys, .pos = mel_vec3(0, 0, 0));
    sys.particles[0].normal = mel_vec3(0, 0, 1);

    mel_verlet_add_particle(&sys, .pos = mel_vec3(1, 0, 0));
    sys.particles[1].normal = mel_vec3(0, 0, -1);

    mel_verlet_apply_wind(&sys, mel_vec3(0, 0, 5));

    MEL_ASSERT_GT(sys.particles[0].accel.z, 0.0f);

    MEL_ASSERT_FLOAT_EQ(sys.particles[1].accel.z, 0.0f, 1e-6f);

    mel_verlet_shutdown(&sys);
}
