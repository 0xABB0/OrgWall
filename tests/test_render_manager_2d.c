#include "../melody/test.harness.h"
#include "../melody/render.manager.h"
#include "../melody/allocator.heap.h"

static void init_mgr(Mel_Render_Manager* mgr)
{
    mel_mgr_init(mgr, .alloc = mel_alloc_heap());
}

MEL_TEST(mgr_2d_init_shutdown, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_mgr(&mgr);

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_alloc_free, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_mgr(&mgr);

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

MEL_TEST(mgr_2d_set_instance_bindings, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);
    Mel_Render_Source* source = (Mel_Render_Source*)0xBEEF;

    mel_mgr_set_instance(&mgr, h, &(Mel_Render_Instance){
        .source = source,
        .material_base_id = 7,
        .material_idx = 3,
        .flags = 0x10u,
        .visibility_mask = 0xFFFF0000u,
    });

    Mel_Render_Instance* instance = mel_mgr_get_instance(&mgr, h);
    MEL_ASSERT_NOT_NULL(instance);
    MEL_ASSERT_EQ(instance->source, source);
    MEL_ASSERT_EQ(instance->material_base_id, 7);
    MEL_ASSERT_EQ(instance->material_idx, 3);
    MEL_ASSERT_EQ(instance->flags, 0x10u);
    MEL_ASSERT_EQ(instance->visibility_mask, 0xFFFF0000u);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_2d_mark_dirty, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);
    mel_mgr_set_instance(&mgr, h, &(Mel_Render_Instance){
        .source = (Mel_Render_Source*)0xCAFE,
        .material_base_id = 1,
        .material_idx = 2,
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
    init_mgr(&mgr);

    u64 before = mel_mgr_mutation_serial(&mgr);
    Mel_Render_Handle h1 = mel_mgr_alloc(&mgr);
    Mel_Render_Handle h2 = mel_mgr_alloc(&mgr);
    MEL_ASSERT_GT(mel_mgr_mutation_serial(&mgr), before);

    mel_mgr_free(&mgr, h1);
    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 1);
    MEL_ASSERT(mel_mgr_alive(&mgr, h2));

    mel_mgr_shutdown(&mgr);
}
