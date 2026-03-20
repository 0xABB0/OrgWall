#include "../melody/test.harness.h"
#include "../melody/render.manager.h"
#include "../melody/render.types.3d.h"
#include "../melody/allocator.heap.h"
#include "../melody/math.mat4.h"
#include "../melody/math.vec3.h"

static void init_3d_mgr(Mel_Render_Manager* mgr)
{
    mel_mgr_init(mgr, .alloc = mel_alloc_heap());
}

MEL_TEST(mgr_init_shutdown, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_3d_mgr(&mgr);

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_alloc_free, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_3d_mgr(&mgr);

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
    init_3d_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);

    Mel_Mat4 model = mel_mat4_translate(mel_vec3(3.0f, 4.0f, 5.0f));
    Mel_Render_Object object = {0};
    object.kind = MEL_RENDER_OBJECT_MESH_3D;
    object.material_base_id = 7;
    object.mesh3d.model = model;
    object.mesh3d.model_inverse = mel_mat4_inverse(model);
    mel_mgr_set_object(&mgr, h, &object);

    Mel_Render_Object* op = mel_mgr_get_object(&mgr, h);
    MEL_ASSERT_NOT_NULL(op);

    Mel_Mat4 product = mel_mat4_mul(op->mesh3d.model, op->mesh3d.model_inverse);
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
    init_3d_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);

    Mel_Geometry_Handle test_mesh = { .handle = { .index = 3, .generation = 1 } };
    mel_mgr_set_object(&mgr, h, &(Mel_Render_Object){
        .kind = MEL_RENDER_OBJECT_MESH_3D,
        .material_base_id = 7,
        .material_idx = 42,
        .flags = MEL_RF_CAST_SHADOW,
        .layer_mask = 0xFF,
        .mesh = test_mesh,
        .bounds = {
            .center = {1.0f, 2.0f, 3.0f},
            .extents = {0.5f, 0.5f, 0.5f},
        },
        .mesh3d = {
            .model = MEL_MAT4_IDENTITY,
            .model_inverse = MEL_MAT4_IDENTITY,
        },
    });

    Mel_Render_Object* op = mel_mgr_get_object(&mgr, h);
    MEL_ASSERT_NOT_NULL(op);
    MEL_ASSERT_FLOAT_EQ(op->bounds.center.x, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->bounds.extents.y, 0.5f, 0.001f);
    MEL_ASSERT_EQ(op->material_base_id, 7);
    MEL_ASSERT_EQ(op->mesh.handle.index, 3);
    MEL_ASSERT_EQ(op->flags, MEL_RF_CAST_SHADOW);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_mark_dirty_changes_serial, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_3d_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);
    mel_mgr_set_object(&mgr, h, &(Mel_Render_Object){
        .kind = MEL_RENDER_OBJECT_MESH_3D,
        .material_base_id = 1,
        .layer_mask = 0xFFFFFFFFu,
        .mesh3d = {
            .model = MEL_MAT4_IDENTITY,
            .model_inverse = MEL_MAT4_IDENTITY,
        },
    };

    u64 before = mel_mgr_mutation_serial(&mgr);
    mel_mgr_mark_dirty(&mgr, h);
    MEL_ASSERT_GT(mel_mgr_mutation_serial(&mgr), before);

    mel_mgr_shutdown(&mgr);
}
