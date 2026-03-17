#include "../melody/test.harness.h"
#include "../melody/gpu.geometry_pool.h"
#include "../melody/allocator.heap.h"

#include <string.h>

typedef struct {
    f32 x, y, z;
    f32 nx, ny, nz;
    f32 u, v;
} Test_Vertex;

static Test_Vertex s_tri_verts[] = {
    { 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.5f, 0.0f },
    { -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f },
    { 1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f },
};

static u32 s_tri_indices[] = { 0, 1, 2 };

static u16 s_tri_indices_u16[] = { 0, 1, 2 };

static Test_Vertex s_quad_verts[] = {
    { -1.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f },
    { -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f },
    { 1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f },
    { 1.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f },
};

static u32 s_quad_indices[] = { 0, 1, 2, 0, 2, 3 };

MEL_TEST(geometry_pool_cpu_init_shutdown, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex),
        .vertex_capacity = sizeof(Test_Vertex) * 64);

    MEL_ASSERT_EQ(pool.storage_mode, MEL_GEOMETRY_STORAGE_CPU);
    MEL_ASSERT_NOT_NULL(pool.vertices.cpu);
    MEL_ASSERT_EQ(pool.vertices.stride, sizeof(Test_Vertex));

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_upload_triangle, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    Mel_Geometry_Upload upload = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
        .indices = s_tri_indices,
        .index_count = 3,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };

    Mel_Geometry_Handle h = mel_geometry_pool_upload(&pool, &upload);
    MEL_ASSERT(mel_geometry_handle_valid(h));

    Mel_Geometry_Region r = mel_geometry_pool_region(&pool, h);
    MEL_ASSERT_EQ(r.vertex_count, 3);
    MEL_ASSERT_EQ(r.index_count, 3);
    MEL_ASSERT_EQ(r.index_type, MEL_GPU_INDEX_TYPE_U32);
    MEL_ASSERT_EQ(r.vertex_offset, 0);
    MEL_ASSERT_EQ(r.index_offset, 0);

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_read_back, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    Mel_Geometry_Upload upload = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
        .indices = s_tri_indices,
        .index_count = 3,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };

    Mel_Geometry_Handle h = mel_geometry_pool_upload(&pool, &upload);

    const Test_Vertex* verts = mel_geometry_pool_cpu_vertices(&pool, h);
    MEL_ASSERT_NOT_NULL(verts);
    MEL_ASSERT_FLOAT_EQ(verts[0].x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(verts[0].y, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(verts[2].x, 1.0f, 0.001f);

    const u32* idx = mel_geometry_pool_cpu_indices(&pool, h);
    MEL_ASSERT_NOT_NULL(idx);
    MEL_ASSERT_EQ(idx[0], 0);
    MEL_ASSERT_EQ(idx[1], 1);
    MEL_ASSERT_EQ(idx[2], 2);

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_multiple_meshes, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    Mel_Geometry_Upload tri = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
        .indices = s_tri_indices,
        .index_count = 3,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };

    Mel_Geometry_Upload quad = {
        .vertices = s_quad_verts,
        .vertex_count = 4,
        .indices = s_quad_indices,
        .index_count = 6,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };

    Mel_Geometry_Handle h_tri = mel_geometry_pool_upload(&pool, &tri);
    Mel_Geometry_Handle h_quad = mel_geometry_pool_upload(&pool, &quad);

    Mel_Geometry_Region r_tri = mel_geometry_pool_region(&pool, h_tri);
    Mel_Geometry_Region r_quad = mel_geometry_pool_region(&pool, h_quad);

    MEL_ASSERT_EQ(r_tri.vertex_count, 3);
    MEL_ASSERT_EQ(r_quad.vertex_count, 4);
    MEL_ASSERT_EQ(r_tri.vertex_offset, 0);
    MEL_ASSERT_EQ(r_quad.vertex_offset, 3);

    MEL_ASSERT_EQ(r_tri.index_offset, 0);
    MEL_ASSERT_EQ(r_quad.index_offset, 3);

    const Test_Vertex* quad_v = mel_geometry_pool_cpu_vertices(&pool, h_quad);
    MEL_ASSERT_FLOAT_EQ(quad_v[0].x, -1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(quad_v[3].x, 1.0f, 0.001f);

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_remove_and_reuse, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    Mel_Geometry_Upload tri = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
        .indices = s_tri_indices,
        .index_count = 3,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };

    Mel_Geometry_Handle h1 = mel_geometry_pool_upload(&pool, &tri);
    Mel_Geometry_Handle h2 = mel_geometry_pool_upload(&pool, &tri);

    Mel_Geometry_Region r1 = mel_geometry_pool_region(&pool, h1);
    u32 first_vertex_offset = r1.vertex_offset;

    mel_geometry_pool_remove(&pool, h1);

    Mel_Geometry_Handle h3 = mel_geometry_pool_upload(&pool, &tri);
    Mel_Geometry_Region r3 = mel_geometry_pool_region(&pool, h3);

    MEL_ASSERT_EQ(r3.vertex_offset, first_vertex_offset);

    MEL_ASSERT(mel_geometry_handle_valid(h2));
    MEL_ASSERT(mel_geometry_handle_valid(h3));

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_u16_indices, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    Mel_Geometry_Upload upload = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
        .indices = s_tri_indices_u16,
        .index_count = 3,
        .index_type = MEL_GPU_INDEX_TYPE_U16,
    };

    Mel_Geometry_Handle h = mel_geometry_pool_upload(&pool, &upload);

    Mel_Geometry_Region r = mel_geometry_pool_region(&pool, h);
    MEL_ASSERT_EQ(r.index_type, MEL_GPU_INDEX_TYPE_U16);
    MEL_ASSERT_EQ(r.index_count, 3);

    const u16* idx = mel_geometry_pool_cpu_indices(&pool, h);
    MEL_ASSERT_NOT_NULL(idx);
    MEL_ASSERT_EQ(idx[0], 0);
    MEL_ASSERT_EQ(idx[1], 1);
    MEL_ASSERT_EQ(idx[2], 2);

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_mixed_index_types, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    Mel_Geometry_Upload u16_mesh = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
        .indices = s_tri_indices_u16,
        .index_count = 3,
        .index_type = MEL_GPU_INDEX_TYPE_U16,
    };

    Mel_Geometry_Upload u32_mesh = {
        .vertices = s_quad_verts,
        .vertex_count = 4,
        .indices = s_quad_indices,
        .index_count = 6,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };

    Mel_Geometry_Handle h16 = mel_geometry_pool_upload(&pool, &u16_mesh);
    Mel_Geometry_Handle h32 = mel_geometry_pool_upload(&pool, &u32_mesh);

    Mel_Geometry_Region r16 = mel_geometry_pool_region(&pool, h16);
    Mel_Geometry_Region r32 = mel_geometry_pool_region(&pool, h32);

    MEL_ASSERT_EQ(r16.index_type, MEL_GPU_INDEX_TYPE_U16);
    MEL_ASSERT_EQ(r32.index_type, MEL_GPU_INDEX_TYPE_U32);

    MEL_ASSERT_EQ(r16.index_offset, 0);
    MEL_ASSERT_EQ(r32.index_offset, 0);

    const u16* idx16 = mel_geometry_pool_cpu_indices(&pool, h16);
    const u32* idx32 = mel_geometry_pool_cpu_indices(&pool, h32);
    MEL_ASSERT_EQ(idx16[2], 2);
    MEL_ASSERT_EQ(idx32[5], 3);

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_no_indices, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    Mel_Geometry_Upload upload = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
    };

    Mel_Geometry_Handle h = mel_geometry_pool_upload(&pool, &upload);

    Mel_Geometry_Region r = mel_geometry_pool_region(&pool, h);
    MEL_ASSERT_EQ(r.vertex_count, 3);
    MEL_ASSERT_EQ(r.index_count, 0);

    const void* idx = mel_geometry_pool_cpu_indices(&pool, h);
    MEL_ASSERT_NULL(idx);

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_region_alignment, .tags = "gpu, render")
{
    Mel_Geometry_Region r = {0};
    MEL_ASSERT_EQ(sizeof(r), 32);
}

MEL_TEST(geometry_pool_cpu_lanes_lazy, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    MEL_ASSERT_NULL(pool.indices_u16.cpu);
    MEL_ASSERT_NULL(pool.indices_u32.cpu);
    MEL_ASSERT_NULL(pool.meshlet_descs.cpu);
    MEL_ASSERT_NULL(pool.meshlet_data.cpu);

    MEL_ASSERT_EQ(pool.indices_u16.capacity, 0);
    MEL_ASSERT_EQ(pool.indices_u32.capacity, 0);

    Mel_Geometry_Upload upload = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
        .indices = s_tri_indices,
        .index_count = 3,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };

    mel_geometry_pool_upload(&pool, &upload);

    MEL_ASSERT_NOT_NULL(pool.indices_u32.cpu);
    MEL_ASSERT_GT(pool.indices_u32.capacity, 0);
    MEL_ASSERT_NULL(pool.indices_u16.cpu);
    MEL_ASSERT_EQ(pool.indices_u16.capacity, 0);

    mel_geometry_pool_shutdown(&pool);
}

MEL_TEST(geometry_pool_cpu_free_list_merge, .tags = "gpu, render")
{
    Mel_Geometry_Pool pool = {0};
    mel_geometry_pool_init(&pool,
        .dev = NULL,
        .alloc = mel_alloc_heap(),
        .vertex_stride = sizeof(Test_Vertex));

    Mel_Geometry_Upload tri = {
        .vertices = s_tri_verts,
        .vertex_count = 3,
        .indices = s_tri_indices,
        .index_count = 3,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };

    Mel_Geometry_Handle h1 = mel_geometry_pool_upload(&pool, &tri);
    Mel_Geometry_Handle h2 = mel_geometry_pool_upload(&pool, &tri);
    Mel_Geometry_Handle h3 = mel_geometry_pool_upload(&pool, &tri);

    mel_geometry_pool_remove(&pool, h1);
    mel_geometry_pool_remove(&pool, h2);

    MEL_ASSERT_EQ(pool.vertices.free_list.count, 1);

    u64 merged_size = (u64)6 * sizeof(Test_Vertex);
    MEL_ASSERT_EQ(pool.vertices.free_list.items[0].size, merged_size);

    (void)h3;
    mel_geometry_pool_shutdown(&pool);
}
