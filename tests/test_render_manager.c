#include "../melody/test.harness.h"
#include "../melody/render.manager.h"
#include "../melody/allocator.heap.h"

static void init_mgr(Mel_Render_Manager* mgr)
{
    mel_mgr_init(mgr, .alloc = mel_alloc_heap());
}

typedef struct {
    u32 value;
} Test_Space;

static const Mel_Render_Space_Type s_test_space_type = {
    .payload_size = sizeof(Test_Space),
};

MEL_TEST(mgr_init_shutdown, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_mgr(&mgr);

    MEL_ASSERT_EQ(mel_mgr_count(&mgr), 0);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_alloc_free, .tags = "render, visual")
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

MEL_TEST(mgr_set_instance_roundtrips, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);
    Mel_Render_Source* source = (Mel_Render_Source*)0x1234;

    mel_mgr_set_instance(&mgr, h, &(Mel_Render_Instance){
        .source = source,
        .flags = 0x55AAu,
        .visibility_mask = 0x0F0F0F0Fu,
    });
    mel_mgr_set_material_bindings(&mgr, h, &(Mel_Render_Material_Binding){
        .slot = 0,
        .material_base_id = 7,
        .material_idx = 42,
        .flags = 0,
    }, 1);

    Mel_Render_Instance* instance = mel_mgr_get_instance(&mgr, h);
    MEL_ASSERT_NOT_NULL(instance);
    MEL_ASSERT_EQ(instance->source, source);
    MEL_ASSERT_EQ(instance->flags, 0x55AAu);
    MEL_ASSERT_EQ(instance->visibility_mask, 0x0F0F0F0Fu);
    MEL_ASSERT_EQ(instance->material_binding_count, 1);

    u32 binding_count = 0;
    const Mel_Render_Material_Binding* bindings = mel_mgr_get_material_bindings(&mgr, h, &binding_count);
    MEL_ASSERT_NOT_NULL(bindings);
    MEL_ASSERT_EQ(binding_count, 1);
    MEL_ASSERT_EQ(bindings[0].material_base_id, 7);
    MEL_ASSERT_EQ(bindings[0].material_idx, 42);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_mark_dirty_changes_serial, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_mgr(&mgr);

    Mel_Render_Handle h = mel_mgr_alloc(&mgr);
    mel_mgr_set_instance(&mgr, h, &(Mel_Render_Instance){
        .source = (Mel_Render_Source*)0x4321,
        .visibility_mask = 0xFFFFFFFFu,
    });
    mel_mgr_set_material_bindings(&mgr, h, &(Mel_Render_Material_Binding){
        .slot = 0,
        .material_base_id = 1,
        .material_idx = 0,
        .flags = 0,
    }, 1);

    u64 before = mel_mgr_mutation_serial(&mgr);
    mel_mgr_mark_dirty(&mgr, h);
    MEL_ASSERT_GT(mel_mgr_mutation_serial(&mgr), before);

    mel_mgr_shutdown(&mgr);
}

MEL_TEST(mgr_space_alloc_payload_free, .tags = "render, visual")
{
    Mel_Render_Manager mgr = {0};
    init_mgr(&mgr);

    Mel_Render_Space_Handle space = mel_mgr_space_alloc(&mgr, &s_test_space_type);
    MEL_ASSERT(mel_render_space_handle_valid(space));
    MEL_ASSERT(mel_mgr_space_alive(&mgr, space));

    Test_Space* payload = mel_mgr_space_payload(&mgr, space, &s_test_space_type);
    MEL_ASSERT_NOT_NULL(payload);
    payload->value = 99;
    MEL_ASSERT_EQ(payload->value, 99);

    mel_mgr_space_free(&mgr, space);
    MEL_ASSERT(!mel_mgr_space_alive(&mgr, space));

    mel_mgr_shutdown(&mgr);
}
