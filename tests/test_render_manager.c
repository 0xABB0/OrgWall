#include "../melody/test.harness.h"
#include "../melody/render.manager.h"
#include "../melody/gpu.device.h"
#include "../melody/allocator.heap.h"
#include "../melody/math.mat4.h"
#include "../melody/math.vec3.h"

#include <math.h>
#include <SDL3/SDL.h>

static Mel_Gpu_Device s_dev;
static bool s_dev_ready = false;

static Mel_Gpu_Device* test_gpu_dev(void)
{
    if (!s_dev_ready)
    {
        SDL_Init(SDL_INIT_VIDEO);
        if (!mel_gpu_device_init(&s_dev, .allocator = mel_alloc_heap()))
            return nullptr;
        s_dev_ready = true;
    }
    return &s_dev;
}

static Mel_Frustum make_test_frustum(void)
{
    Mel_Mat4 proj = mel_mat4_perspective(M_PI / 2.0f, 1.0f, 0.1f, 100.0f);
    Mel_Mat4 view = mel_mat4_look_at(
        mel_vec3(0, 0, 5), mel_vec3(0, 0, 0), mel_vec3(0, 1, 0));
    Mel_Mat4 vp = mel_mat4_mul(proj, view);

    Mel_Frustum f;
    Mel_Vec4 r0 = mel_vec4(vp.m[0][0], vp.m[0][1], vp.m[0][2], vp.m[0][3]);
    Mel_Vec4 r1 = mel_vec4(vp.m[1][0], vp.m[1][1], vp.m[1][2], vp.m[1][3]);
    Mel_Vec4 r2 = mel_vec4(vp.m[2][0], vp.m[2][1], vp.m[2][2], vp.m[2][3]);
    Mel_Vec4 r3 = mel_vec4(vp.m[3][0], vp.m[3][1], vp.m[3][2], vp.m[3][3]);

    f.planes[0] = mel_plane_normalize(mel_vec4_add(r3, r0));
    f.planes[1] = mel_plane_normalize(mel_vec4_sub(r3, r0));
    f.planes[2] = mel_plane_normalize(mel_vec4_add(r3, r1));
    f.planes[3] = mel_plane_normalize(mel_vec4_sub(r3, r1));
    f.planes[4] = mel_plane_normalize(mel_vec4_add(r3, r2));
    f.planes[5] = mel_plane_normalize(mel_vec4_sub(r3, r2));

    return f;
}

MEL_TEST(mgr_init_shutdown, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    mel_mgr_init(&mgr, .dev = test_gpu_dev(), .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);
    MEL_ASSERT_NOT_NULL(mel_mgr_transform_buffer(&mgr));
    MEL_ASSERT_NOT_NULL(mel_mgr_bounds_buffer(&mgr));
    MEL_ASSERT_NOT_NULL(mel_mgr_info_buffer(&mgr));

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_alloc_free, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    mel_mgr_init(&mgr, .dev = test_gpu_dev(), .alloc = mel_alloc_heap());

    Mel_Render_Handle h1 = mel_mgr_alloc(&mgr);
    Mel_Render_Handle h2 = mel_mgr_alloc(&mgr);
    Mel_Render_Handle h3 = mel_mgr_alloc(&mgr);

    MEL_ASSERT(mel_render_handle_valid(h1));
    MEL_ASSERT(mel_render_handle_valid(h2));
    MEL_ASSERT(mel_render_handle_valid(h3));
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 3);

    mel_mgr_free(&mgr, h2);
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 2);

    mel_mgr_free(&mgr, h1);
    mel_mgr_free(&mgr, h3);
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_set_transform_computes_inverse, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    mel_mgr_init(&mgr,
        .dev = test_gpu_dev(),
        .alloc = mel_alloc_heap(),
        .cpu_access_mask = MEL_MGR_TRANSFORMS);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);

    Mel_Mat4 model = mel_mat4_translate(mel_vec3(3.0f, 4.0f, 5.0f));
    mel_mgr_set_transform(&mgr, h, model);

    Mel_Render_Transform* t = mel_storage_pool_get(&mgr.transforms, h.handle);
    MEL_ASSERT_NOT_NULL(t);

    Mel_Mat4 product = mel_mat4_mul(t->model, t->model_inverse);
    for (i32 i = 0; i < 4; i++)
    {
        for (i32 j = 0; j < 4; j++)
        {
            f32 expected = (i == j) ? 1.0f : 0.0f;
            MEL_ASSERT_FLOAT_EQ(product.m[i][j], expected, 0.0001f);
        }
    }

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_set_bounds_and_info, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    mel_mgr_init(&mgr,
        .dev = test_gpu_dev(),
        .alloc = mel_alloc_heap(),
        .cpu_access_mask = MEL_MGR_BOUNDS | MEL_MGR_INFOS);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);

    Mel_Render_Bounds b = {
        .center = mel_vec3(1.0f, 2.0f, 3.0f),
        .extents = mel_vec3(0.5f, 0.5f, 0.5f),
    };
    mel_mgr_set_bounds(&mgr, h, b);

    Mel_Render_Info info = {
        .material_base_id = 7,
        .material_idx = 42,
        .mesh_idx = 3,
        .flags = MEL_RF_CAST_SHADOW,
        .layer_mask = 0xFF,
    };
    mel_mgr_set_info(&mgr, h, info);

    Mel_Render_Bounds* bp = mel_storage_pool_get(&mgr.bounds, h.handle);
    MEL_ASSERT_FLOAT_EQ(bp->center.x, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(bp->extents.y, 0.5f, 0.001f);

    Mel_Render_Info* ip = mel_storage_pool_get(&mgr.infos, h.handle);
    MEL_ASSERT_EQ(ip->material_base_id, 7);
    MEL_ASSERT_EQ(ip->mesh_idx, 3);
    MEL_ASSERT_EQ(ip->flags, MEL_RF_CAST_SHADOW);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_cull_visible, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    mel_mgr_init(&mgr,
        .dev = test_gpu_dev(),
        .alloc = mel_alloc_heap(),
        .cpu_access_mask = MEL_MGR_BOUNDS);

    Mel_Render_Handle h_visible = mel_mgr_alloc(&mgr);
    mel_mgr_set_bounds(&mgr, h_visible, (Mel_Render_Bounds){
        .center = mel_vec3(0, 0, 0),
        .extents = mel_vec3(1, 1, 1),
    });

    Mel_Render_Handle h_behind = mel_mgr_alloc(&mgr);
    mel_mgr_set_bounds(&mgr, h_behind, (Mel_Render_Bounds){
        .center = mel_vec3(0, 0, 200),
        .extents = mel_vec3(1, 1, 1),
    });

    Mel_Frustum frustum = make_test_frustum();
    Mel_BitSet visibility = {0};
    mel_bitset_init(&visibility, 64, mel_alloc_heap());

    mel_mgr_cull(&mgr, &frustum, &visibility);

    MEL_ASSERT(mel_bitset_get(&visibility, 0));
    MEL_ASSERT(!mel_bitset_get(&visibility, 1));

    mel_bitset_free(&visibility);
    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_upload_dirty, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    mel_mgr_init(&mgr, .dev = test_gpu_dev(), .alloc = mel_alloc_heap());

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);
    mel_mgr_set_transform(&mgr, h, mel_mat4_translate(mel_vec3(1, 2, 3)));
    mel_mgr_set_bounds(&mgr, h, (Mel_Render_Bounds){
        .center = mel_vec3(1, 2, 3),
        .extents = mel_vec3(1, 1, 1),
    });
    mel_mgr_set_info(&mgr, h, (Mel_Render_Info){
        .material_base_id = 1,
        .mesh_idx = 0,
        .layer_mask = 0xFFFFFFFF,
    });

    mel_mgr_upload_dirty(&mgr);

    MEL_ASSERT(!mel_storage_pool_is_dirty(&mgr.transforms));
    MEL_ASSERT(!mel_storage_pool_is_dirty(&mgr.bounds));
    MEL_ASSERT(!mel_storage_pool_is_dirty(&mgr.infos));

    mel_mgr_shutdown(&mgr);
}
