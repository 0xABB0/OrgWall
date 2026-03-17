#include "../melody/test.harness.h"
#include "../melody/render.cull.h"
#include "../melody/allocator.heap.h"

static Mel_Frustum make_box_frustum(f32 half_x, f32 half_y, f32 half_z)
{
    Mel_Frustum f;
    f.planes[0] = mel_plane( 1,  0,  0, half_x);
    f.planes[1] = mel_plane(-1,  0,  0, half_x);
    f.planes[2] = mel_plane( 0,  1,  0, half_y);
    f.planes[3] = mel_plane( 0, -1,  0, half_y);
    f.planes[4] = mel_plane( 0,  0,  1, half_z);
    f.planes[5] = mel_plane( 0,  0, -1, half_z);
    return f;
}

MEL_TEST(cull_aabb_inside_frustum, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(10.0f, 10.0f, 10.0f);
    Mel_AABB aabb = { .center = MEL_VEC3(0, 0, 0), .extents = MEL_VEC3(1, 1, 1) };
    MEL_ASSERT(mel_aabb_vs_frustum(aabb, &frustum));
}

MEL_TEST(cull_aabb_outside_frustum, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(10.0f, 10.0f, 10.0f);
    Mel_AABB aabb = { .center = MEL_VEC3(20, 0, 0), .extents = MEL_VEC3(1, 1, 1) };
    MEL_ASSERT(!mel_aabb_vs_frustum(aabb, &frustum));
}

MEL_TEST(cull_aabb_intersecting_frustum, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(10.0f, 10.0f, 10.0f);
    Mel_AABB aabb = { .center = MEL_VEC3(10, 0, 0), .extents = MEL_VEC3(2, 2, 2) };
    MEL_ASSERT(mel_aabb_vs_frustum(aabb, &frustum));
}

MEL_TEST(cull_batch_mixed, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(5.0f, 5.0f, 5.0f);

    Mel_AABB bounds[] = {
        { .center = MEL_VEC3( 0,  0,  0), .extents = MEL_VEC3(1, 1, 1) },
        { .center = MEL_VEC3(20,  0,  0), .extents = MEL_VEC3(1, 1, 1) },
        { .center = MEL_VEC3( 0, 20,  0), .extents = MEL_VEC3(1, 1, 1) },
        { .center = MEL_VEC3( 3,  3,  3), .extents = MEL_VEC3(1, 1, 1) },
        { .center = MEL_VEC3( 0,  0, -20), .extents = MEL_VEC3(1, 1, 1) },
    };

    u32 count = sizeof(bounds) / sizeof(bounds[0]);
    Mel_BitSet vis;
    mel_bitset_init(&vis, count, mel_alloc_heap());

    mel_frustum_cull(bounds, count, &frustum, &vis);

    MEL_ASSERT(mel_bitset_get(&vis, 0));
    MEL_ASSERT(!mel_bitset_get(&vis, 1));
    MEL_ASSERT(!mel_bitset_get(&vis, 2));
    MEL_ASSERT(mel_bitset_get(&vis, 3));
    MEL_ASSERT(!mel_bitset_get(&vis, 4));

    MEL_ASSERT_EQ(mel_bitset_count_set(&vis), (usize)2);

    mel_bitset_free(&vis);
}

MEL_TEST(cull_all_visible, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(100.0f, 100.0f, 100.0f);

    Mel_AABB bounds[64];
    for (u32 i = 0; i < 64; i++)
    {
        bounds[i] = (Mel_AABB){
            .center = MEL_VEC3((f32)(i % 8) * 2.0f, (f32)(i / 8) * 2.0f, 0),
            .extents = MEL_VEC3(0.5f, 0.5f, 0.5f),
        };
    }

    Mel_BitSet vis;
    mel_bitset_init(&vis, 64, mel_alloc_heap());
    mel_frustum_cull(bounds, 64, &frustum, &vis);

    MEL_ASSERT(mel_bitset_all(&vis));
    MEL_ASSERT_EQ(mel_bitset_count_set(&vis), (usize)64);

    mel_bitset_free(&vis);
}

MEL_TEST(cull_none_visible, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(1.0f, 1.0f, 1.0f);

    Mel_AABB bounds[32];
    for (u32 i = 0; i < 32; i++)
    {
        bounds[i] = (Mel_AABB){
            .center = MEL_VEC3(100.0f + (f32)i, 100.0f, 100.0f),
            .extents = MEL_VEC3(0.1f, 0.1f, 0.1f),
        };
    }

    Mel_BitSet vis;
    mel_bitset_init(&vis, 32, mel_alloc_heap());
    mel_frustum_cull(bounds, 32, &frustum, &vis);

    MEL_ASSERT(mel_bitset_none(&vis));
    MEL_ASSERT_EQ(mel_bitset_count_set(&vis), (usize)0);

    mel_bitset_free(&vis);
}

MEL_TEST(cull_single_plane, .tags = "render")
{
    Mel_Plane plane = mel_plane(1, 0, 0, 5);
    Mel_AABB inside = { .center = MEL_VEC3(0, 0, 0), .extents = MEL_VEC3(1, 1, 1) };
    Mel_AABB outside = { .center = MEL_VEC3(-10, 0, 0), .extents = MEL_VEC3(1, 1, 1) };

    MEL_ASSERT(mel_aabb_vs_plane(inside, plane));
    MEL_ASSERT(!mel_aabb_vs_plane(outside, plane));
}

MEL_TEST(cull_aabb_exactly_on_plane, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(5.0f, 5.0f, 5.0f);
    Mel_AABB aabb = { .center = MEL_VEC3(5.0f, 0, 0), .extents = MEL_VEC3(0, 0, 0) };
    MEL_ASSERT(mel_aabb_vs_frustum(aabb, &frustum));
}

MEL_TEST(cull_aabb_touching_plane_with_extents, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(5.0f, 5.0f, 5.0f);
    Mel_AABB touching = { .center = MEL_VEC3(6.0f, 0, 0), .extents = MEL_VEC3(1.0f, 0, 0) };
    MEL_ASSERT(mel_aabb_vs_frustum(touching, &frustum));

    Mel_AABB just_outside = { .center = MEL_VEC3(6.1f, 0, 0), .extents = MEL_VEC3(1.0f, 0, 0) };
    MEL_ASSERT(!mel_aabb_vs_frustum(just_outside, &frustum));
}

MEL_TEST(cull_zero_size_aabb, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(10.0f, 10.0f, 10.0f);

    Mel_AABB origin_point = { .center = MEL_VEC3(0, 0, 0), .extents = MEL_VEC3(0, 0, 0) };
    MEL_ASSERT(mel_aabb_vs_frustum(origin_point, &frustum));

    Mel_AABB outside_point = { .center = MEL_VEC3(20, 0, 0), .extents = MEL_VEC3(0, 0, 0) };
    MEL_ASSERT(!mel_aabb_vs_frustum(outside_point, &frustum));
}

MEL_TEST(cull_degenerate_frustum_zero_size, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(0.0f, 0.0f, 0.0f);

    Mel_AABB at_origin = { .center = MEL_VEC3(0, 0, 0), .extents = MEL_VEC3(0, 0, 0) };
    MEL_ASSERT(mel_aabb_vs_frustum(at_origin, &frustum));

    Mel_AABB offset = { .center = MEL_VEC3(0.1f, 0, 0), .extents = MEL_VEC3(0, 0, 0) };
    MEL_ASSERT(!mel_aabb_vs_frustum(offset, &frustum));
}

MEL_TEST(cull_large_object_count, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(50.0f, 50.0f, 50.0f);

    u32 count = 1024;
    Mel_AABB bounds[1024];
    for (u32 i = 0; i < count; i++)
    {
        f32 x = (f32)(i % 32) * 3.0f;
        f32 y = (f32)(i / 32) * 3.0f;
        bounds[i] = (Mel_AABB){
            .center = MEL_VEC3(x, y, 0),
            .extents = MEL_VEC3(1, 1, 1),
        };
    }

    Mel_BitSet vis;
    mel_bitset_init(&vis, count, mel_alloc_heap());
    mel_frustum_cull(bounds, count, &frustum, &vis);

    usize visible_count = mel_bitset_count_set(&vis);
    MEL_ASSERT(visible_count > 0);
    MEL_ASSERT(visible_count < count);

    for (u32 i = 0; i < count; i++)
    {
        bool expected = mel_aabb_vs_frustum(bounds[i], &frustum);
        MEL_ASSERT_EQ(mel_bitset_get(&vis, i), expected);
    }

    mel_bitset_free(&vis);
}

MEL_TEST(cull_aabb_on_corner_of_frustum, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(5.0f, 5.0f, 5.0f);

    Mel_AABB at_corner = { .center = MEL_VEC3(5.0f, 5.0f, 5.0f), .extents = MEL_VEC3(0, 0, 0) };
    MEL_ASSERT(mel_aabb_vs_frustum(at_corner, &frustum));

    Mel_AABB past_corner = { .center = MEL_VEC3(5.1f, 5.1f, 5.1f), .extents = MEL_VEC3(0, 0, 0) };
    MEL_ASSERT(!mel_aabb_vs_frustum(past_corner, &frustum));
}

MEL_TEST(cull_asymmetric_frustum, .tags = "render")
{
    Mel_Frustum frustum;
    frustum.planes[0] = mel_plane( 1,  0,  0, 2);
    frustum.planes[1] = mel_plane(-1,  0,  0, 10);
    frustum.planes[2] = mel_plane( 0,  1,  0, 5);
    frustum.planes[3] = mel_plane( 0, -1,  0, 5);
    frustum.planes[4] = mel_plane( 0,  0,  1, 5);
    frustum.planes[5] = mel_plane( 0,  0, -1, 5);

    Mel_AABB inside_both = { .center = MEL_VEC3(0, 0, 0), .extents = MEL_VEC3(1, 1, 1) };
    MEL_ASSERT(mel_aabb_vs_frustum(inside_both, &frustum));

    Mel_AABB right_of_asym = { .center = MEL_VEC3(8, 0, 0), .extents = MEL_VEC3(1, 1, 1) };
    MEL_ASSERT(mel_aabb_vs_frustum(right_of_asym, &frustum));

    Mel_AABB left_outside = { .center = MEL_VEC3(-5, 0, 0), .extents = MEL_VEC3(1, 1, 1) };
    MEL_ASSERT(!mel_aabb_vs_frustum(left_outside, &frustum));
}

MEL_TEST(cull_batch_single_object, .tags = "render")
{
    Mel_Frustum frustum = make_box_frustum(5.0f, 5.0f, 5.0f);

    Mel_AABB bounds[1] = {
        { .center = MEL_VEC3(0, 0, 0), .extents = MEL_VEC3(1, 1, 1) },
    };

    Mel_BitSet vis;
    mel_bitset_init(&vis, 1, mel_alloc_heap());
    mel_frustum_cull(bounds, 1, &frustum, &vis);

    MEL_ASSERT(mel_bitset_get(&vis, 0));
    MEL_ASSERT_EQ(mel_bitset_count_set(&vis), (usize)1);

    mel_bitset_free(&vis);
}
