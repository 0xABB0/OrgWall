#include "../melody/test.harness.h"
#include "../melody/render.manager.h"
#include "../melody/render.types.3d.h"
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

static void init_3d_mgr(Mel_Render_Manager* mgr, Mel_Gpu_Device* dev)
{
    Mel_Mgr_Pool_Desc pools[] = {
        { .item_size = sizeof(Mel_Render_Transform) },
        { .item_size = sizeof(Mel_Render_Bounds) },
        { .item_size = sizeof(Mel_Render_Info) },
    };
    mel_mgr_init(mgr, .dev = dev, .alloc = mel_alloc_heap(), .pools = pools, .pool_count = MEL_3D_POOL_COUNT);
}

MEL_TEST(mgr_init_shutdown, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_3d_mgr(&mgr, test_gpu_dev());

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);
    MEL_ASSERT_NOT_NULL(mel_mgr_pool_buffer(&mgr, MEL_3D_POOL_TRANSFORMS));
    MEL_ASSERT_NOT_NULL(mel_mgr_pool_buffer(&mgr, MEL_3D_POOL_BOUNDS));
    MEL_ASSERT_NOT_NULL(mel_mgr_pool_buffer(&mgr, MEL_3D_POOL_INFOS));

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_alloc_free, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_3d_mgr(&mgr, test_gpu_dev());

    Mel_Render_Handle h1 = mel_mgr_alloc(&mgr, 0);
    Mel_Render_Handle h2 = mel_mgr_alloc(&mgr, 0);
    Mel_Render_Handle h3 = mel_mgr_alloc(&mgr, 0);

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
    init_3d_mgr(&mgr, test_gpu_dev());

    Mel_Render_Handle h = mel_mgr_alloc(&mgr, 0);

    Mel_Mat4 model = mel_mat4_translate(mel_vec3(3.0f, 4.0f, 5.0f));
    Mel_Render_Transform t = { .model = model, .model_inverse = mel_mat4_inverse(model) };
    mel_mgr_set(&mgr, MEL_3D_POOL_TRANSFORMS, h, &t);

    Mel_Render_Transform* tp = mel_mgr_get(&mgr, MEL_3D_POOL_TRANSFORMS, h);
    MEL_ASSERT_NOT_NULL(tp);

    Mel_Mat4 product = mel_mat4_mul(tp->model, tp->model_inverse);
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
    init_3d_mgr(&mgr, test_gpu_dev());

    Mel_Render_Handle h = mel_mgr_alloc(&mgr, 7);

    Mel_Render_Bounds b = {
        .center = mel_vec3(1.0f, 2.0f, 3.0f),
        .extents = mel_vec3(0.5f, 0.5f, 0.5f),
    };
    mel_mgr_set(&mgr, MEL_3D_POOL_BOUNDS, h, &b);

    Mel_Geometry_Handle test_mesh = { .handle = { .index = 3, .generation = 1 } };
    Mel_Render_Info info = {
        .material_base_id = 7,
        .material_idx = 42,
        .mesh = test_mesh,
        .flags = MEL_RF_CAST_SHADOW,
        .layer_mask = 0xFF,
    };
    mel_mgr_set(&mgr, MEL_3D_POOL_INFOS, h, &info);

    Mel_Render_Bounds* bp = mel_mgr_get(&mgr, MEL_3D_POOL_BOUNDS, h);
    MEL_ASSERT_FLOAT_EQ(bp->center.x, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(bp->extents.y, 0.5f, 0.001f);

    Mel_Render_Info* ip = mel_mgr_get(&mgr, MEL_3D_POOL_INFOS, h);
    MEL_ASSERT_EQ(ip->material_base_id, 7);
    MEL_ASSERT_EQ(ip->mesh.handle.index, 3);
    MEL_ASSERT_EQ(ip->flags, MEL_RF_CAST_SHADOW);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_upload_dirty, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_3d_mgr(&mgr, test_gpu_dev());

    Mel_Render_Handle h = mel_mgr_alloc(&mgr, 1);
    Mel_Mat4 model = mel_mat4_translate(mel_vec3(1, 2, 3));
    Mel_Render_Transform t = { .model = model, .model_inverse = mel_mat4_inverse(model) };
    mel_mgr_set(&mgr, MEL_3D_POOL_TRANSFORMS, h, &t);
    mel_mgr_set(&mgr, MEL_3D_POOL_BOUNDS, h, &(Mel_Render_Bounds){
        .center = mel_vec3(1, 2, 3),
        .extents = mel_vec3(1, 1, 1),
    });
    mel_mgr_set(&mgr, MEL_3D_POOL_INFOS, h, &(Mel_Render_Info){
        .material_base_id = 1,
        .mesh = MEL_GEOMETRY_HANDLE_NULL,
        .layer_mask = 0xFFFFFFFF,
    });

    mel_mgr_upload_dirty(&mgr);
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 1);

    mel_mgr_shutdown(&mgr);
}
