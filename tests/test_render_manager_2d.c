#include "../melody/test.harness.h"
#include "../melody/render.manager.h"
#include "../melody/render.types.2d.h"
#include "../melody/allocator.heap.h"
#include "../melody/math.vec2.h"
#include "../melody/math.vec4.h"
#include "../melody/math.geo.rect.h"

static void init_2d_mgr(Mel_Render_Manager* mgr)
{
    mel_mgr_init(mgr, .alloc = mel_alloc_heap());
}

MEL_TEST(mgr_2d_init_shutdown, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr);

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_alloc_free, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr);

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

MEL_TEST(mgr_2d_set_transform, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);

    Mel_Render_Object object = {0};
    object.kind = MEL_RENDER_OBJECT_SPRITE_2D;
    object.material_base_id = 7;
    object.uv = mel_rect(0.0f, 0.0f, 1.0f, 1.0f);
    object.color = mel_vec4(1.0f, 0.5f, 0.25f, 1.0f);
    object.texture_idx = 42;
    object.sprite2d.pos = mel_vec2(10.0f, 20.0f);
    object.sprite2d.scale = mel_vec2(2.0f, 3.0f);
    object.sprite2d.rotation = 1.5f;
    object.sprite2d.depth = 5.0f;
    object.sprite2d.flags = MEL_RF2D_FLIP_X;
    mel_mgr_set_object(&mgr, h, &object);

    Mel_Render_Object* op = mel_mgr_get_object(&mgr, h);
    MEL_ASSERT_NOT_NULL(op);
    MEL_ASSERT_EQ(op->kind, MEL_RENDER_OBJECT_SPRITE_2D);
    MEL_ASSERT_FLOAT_EQ(op->sprite2d.pos.x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->sprite2d.pos.y, 20.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->sprite2d.scale.x, 2.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->sprite2d.scale.y, 3.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->sprite2d.rotation, 1.5f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->sprite2d.depth, 5.0f, 0.001f);
    MEL_ASSERT_EQ(op->sprite2d.flags, MEL_RF2D_FLIP_X);
    MEL_ASSERT_EQ(op->texture_idx, 42);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_set_sprite_info, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);

    Mel_Render_Object object = {0};
    object.kind = MEL_RENDER_OBJECT_SPRITE_2D;
    object.material_base_id = 7;
    object.material_idx = 3;
    object.texture_idx = 42;
    object.uv = mel_rect(0.0f, 0.0f, 1.0f, 1.0f);
    object.color = mel_vec4(1.0f, 0.5f, 0.25f, 1.0f);
    mel_mgr_set_object(&mgr, h, &object);

    Mel_Render_Object* op = mel_mgr_get_object(&mgr, h);
    MEL_ASSERT_NOT_NULL(op);
    MEL_ASSERT_FLOAT_EQ(op->uv.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->uv.w, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->color.r, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(op->color.g, 0.5f, 0.001f);
    MEL_ASSERT_EQ(op->texture_idx, 42);
    MEL_ASSERT_EQ(op->material_base_id, 7);
    MEL_ASSERT_EQ(op->material_idx, 3);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_mark_dirty, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);
    mel_mgr_set_object(&mgr, h, &(Mel_Render_Object){
        .kind = MEL_RENDER_OBJECT_SPRITE_2D,
        .material_base_id = 1,
        .uv = {0, 0, 1, 1},
        .color = {{1, 1, 1, 1}},
        .sprite2d = {
            .pos = {1.0f, 2.0f},
            .scale = MEL_VEC2_ONE,
        },
    });

    u64 before = mel_mgr_mutation_serial(&mgr);
    mel_mgr_mark_dirty(&mgr, h);

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 1);
    MEL_ASSERT_GT(mel_mgr_mutation_serial(&mgr), before);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_mutation_serial_changes, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_2d_mgr(&mgr);

    u64 before = mel_mgr_mutation_serial(&mgr);
    Mel_Render_Handle h1 = mel_mgr_alloc(&mgr);
    Mel_Render_Handle h2 = mel_mgr_alloc(&mgr);
    MEL_ASSERT_GT(mel_mgr_mutation_serial(&mgr), before);

    mel_mgr_free(&mgr, h1);
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 1);
    MEL_ASSERT(mel_mgr_alive(&mgr, h2));

    mel_mgr_shutdown(&mgr);
}
