#include "../melody/test.harness.h"
#include "../melody/render.source.h"
#include "../melody/render.view.h"
#include "../melody/render.frame_recipe.h"
#include "../melody/render.frame_plan.h"
#include "../melody/render.graph.h"
#include "../melody/render.list.h"
#include "../melody/render.camera.h"
#include "../melody/sprite.pass.h"
#include "../melody/text.pass.h"
#include "../melody/swapchain.h"
#include "../melody/math.mat4.h"
#include "../melody/allocator.heap.h"
#include "../melody/string.str8.h"

typedef struct {
    bool acquire_result;
    bool present_result;
} Mock_Swapchain;

static bool mock_acquire(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)sc;
    (void)dev;
    return ((Mock_Swapchain*)sc->data)->acquire_result;
}

static bool mock_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev, VkCommandBuffer cmd, VkFence fence)
{
    (void)dev;
    (void)cmd;
    (void)fence;
    return ((Mock_Swapchain*)sc->data)->present_result;
}

static void mock_resize(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height)
{
    (void)dev;
    sc->extent = (VkExtent2D){ width, height };
}

static void mock_shutdown(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev;
    if (sc->data)
        mel_dealloc(mel_alloc_heap(), sc->data);
    sc->data = nullptr;
}

static const Mel_Swapchain_Vtable s_mock_vtable = {
    .acquire = mock_acquire,
    .present = mock_present,
    .resize = mock_resize,
    .shutdown = mock_shutdown,
};

static Mel_Swapchain_Handle make_mock_swapchain(void)
{
    Mock_Swapchain* mock = mel_alloc_type(mel_alloc_heap(), Mock_Swapchain);
    *mock = (Mock_Swapchain){
        .acquire_result = true,
        .present_result = true,
    };

    Mel_Swapchain_Entry entry = {
        .swapchain = {
            .vtable = &s_mock_vtable,
            .data = mock,
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .extent = { 640, 480 },
            .image_count = 2,
        },
    };

    return mel_swapchain_registry_insert(&entry);
}

static void test_user_technique_pass(Mel_Render_Pass_Ctx* ctx)
{
    (void)ctx;
}

static Mel_Technique_Check_Result test_user_support_mesh_shader(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    bool ok = ctx->dev && ctx->dev->capabilities.mesh_shader;
    return (Mel_Technique_Check_Result){
        .ok = ok,
        .kind = ok ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_CAPABILITY_UNAVAILABLE,
        .reason = ok ? S8("mesh shader capability available") : S8("mesh shader capability unavailable"),
    };
}

static Mel_Technique_Check_Result test_user_support_always(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    (void)ctx;
    return (Mel_Technique_Check_Result){
        .ok = true,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8("supported by current device"),
    };
}

static Mel_Technique_Check_Result test_user_match_always(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    (void)ctx;
    return (Mel_Technique_Check_Result){
        .ok = true,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8("test view matches"),
    };
}

static Mel_Technique_Compile_Result test_user_compile(const Mel_Technique_Compile_Ctx* ctx)
{
    bool ok = mel_frame_plan_add_pass(ctx->plan_ctx, ctx->technique->name,
        test_user_technique_pass, nullptr, nullptr, nullptr, nullptr);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

MEL_TEST(frame_recipe_compiles_sprite_view_to_graph_pass, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .name = S8("sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Source_Handle source = mel_source_from_render_list(&list, MEL_SCHEMA_SPRITE);

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };

    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = S8("main"),
        .camera = &camera,
        .clear_color_enabled = true,
    });
    mel_view_attach_source(view, source);

    Mel_Swapchain_Handle swapchain = make_mock_swapchain();

    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("test"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("test"));
    mel_frame_recipe_use_technique(recipe, view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(recipe, view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)1);
    MEL_ASSERT_EQ(graph.sorted_order.count, (usize)1);
    MEL_ASSERT(graph.passes.items[0].read_lists != nullptr);
    MEL_ASSERT(graph.passes.items[0].read_lists[0] == &list);

    ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)1);

    mel_frame_recipe_destroy(recipe);
    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(view);
    mel_source_destroy(source);
    mel_render_list_shutdown(&list);
}

MEL_TEST(frame_recipe_compiles_text_view_to_graph_pass, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .name = S8("text"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .alloc = mel_alloc_heap());

    Mel_Source_Handle source = mel_source_from_render_list(&list, MEL_SCHEMA_TEXT);

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };

    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = S8("text_view"),
        .camera = &camera,
        .clear_color_enabled = true,
    });
    mel_view_attach_source(view, source);

    Mel_Swapchain_Handle swapchain = make_mock_swapchain();

    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("text"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("text"));
    mel_frame_recipe_use_technique(recipe, view, MEL_TECHNIQUE_TEXT);
    mel_frame_recipe_present(recipe, view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)1);
    MEL_ASSERT(graph.passes.items[0].read_lists != nullptr);
    MEL_ASSERT(graph.passes.items[0].read_lists[0] == &list);

    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(view);
    mel_source_destroy(source);
    mel_render_list_shutdown(&list);
}

MEL_TEST(frame_plan_reports_resolved_registered_techniques, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .name = S8("sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Source_Handle source = mel_source_from_render_list(&list, MEL_SCHEMA_SPRITE);

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };

    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = S8("main"),
        .camera = &camera,
        .clear_color_enabled = true,
    });
    mel_view_attach_source(view, source);

    Mel_Swapchain_Handle swapchain = make_mock_swapchain();

    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("resolved"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("resolved"));
    mel_frame_recipe_use_technique(recipe, view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(recipe, view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(plan), (u32)1);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    ok = mel_frame_plan_resolved_technique_at(plan, 0, &resolved);
    MEL_ASSERT(ok);
    MEL_ASSERT(resolved.family == MEL_TECHNIQUE_SPRITE);
    MEL_ASSERT_EQ(resolved.binding_index, (u32)0);
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("sprite")));

    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(view);
    mel_source_destroy(source);
    mel_render_list_shutdown(&list);
}

MEL_TEST(frame_plan_prefers_higher_priority_supported_user_variant, .tags = "render")
{
    const str8 accel_name = S8("user.mesh.accel");
    const str8 fallback_name = S8("user.mesh.fallback");

    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_USER_BASE,
        .name = accel_name,
        .priority = 200,
        .supports = test_user_support_mesh_shader,
        .matches = test_user_match_always,
        .compile = test_user_compile,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_USER_BASE,
        .name = fallback_name,
        .priority = 100,
        .supports = test_user_support_always,
        .matches = test_user_match_always,
        .compile = test_user_compile,
    });

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = S8("user_view"),
        .camera = &camera,
        .clear_color_enabled = true,
    });
    Mel_Swapchain_Handle swapchain = make_mock_swapchain();

    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("user_variant"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("user_variant"));
    mel_frame_recipe_use_technique(recipe, view, MEL_TECHNIQUE_USER_BASE);
    mel_frame_recipe_present(recipe, view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    fake_dev.capabilities.mesh_shader = true;
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(plan), (u32)1);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, accel_name));

    Mel_Frame_Plan_Technique_Diagnostic first = {0};
    Mel_Frame_Plan_Technique_Diagnostic second = {0};
    MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(plan, 0, &first));
    MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(plan, 1, &second));
    MEL_ASSERT(str8_ieq(first.technique_name, accel_name));
    MEL_ASSERT(first.supported);
    MEL_ASSERT(first.matched);
    MEL_ASSERT(first.selected);
    MEL_ASSERT(str8_ieq(second.technique_name, fallback_name));
    MEL_ASSERT(second.supported);
    MEL_ASSERT(second.matched);
    MEL_ASSERT(!second.selected);
    MEL_ASSERT_EQ(second.reason_kind, (u32)MEL_TECHNIQUE_CHECK_POLICY_SKIPPED);

    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(view);
    mel_render_technique_unregister(MEL_TECHNIQUE_USER_BASE, accel_name);
    mel_render_technique_unregister(MEL_TECHNIQUE_USER_BASE, fallback_name);
}

MEL_TEST(frame_plan_compile_failure_rolls_back_generated_passes, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .name = S8("sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Source_Handle source = mel_source_from_render_list(&list, MEL_SCHEMA_SPRITE);

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };

    Mel_View_Handle good_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("good"),
        .camera = &camera,
        .clear_color_enabled = true,
    });
    mel_view_attach_source(good_view, source);

    Mel_View_Handle bad_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("bad"),
        .clear_color_enabled = true,
    });
    mel_view_attach_source(bad_view, source);

    Mel_Swapchain_Handle swapchain = make_mock_swapchain();

    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("rollback"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("rollback"));
    mel_frame_recipe_use_technique(recipe, good_view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_use_technique(recipe, bad_view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(recipe, good_view, swapchain);
    mel_frame_recipe_overlay(recipe, bad_view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2);
    MEL_ASSERT(!ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)0);

    mel_frame_recipe_disable_technique(recipe, bad_view, MEL_TECHNIQUE_SPRITE);

    ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)1);

    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(bad_view);
    mel_view_destroy(good_view);
    mel_source_destroy(source);
    mel_render_list_shutdown(&list);
}

MEL_TEST(frame_plan_refresh_updates_view_parameters_without_recompile, .tags = "render")
{
    Mel_Render_List list;
    mel_render_list_init(&list,
        .name = S8("sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Source_Handle source = mel_source_from_render_list(&list, MEL_SCHEMA_SPRITE);

    Mel_Camera camera_a = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Camera camera_b = {
        .view = mel_mat4_translate((Mel_Vec3){ .x = 3.0f }),
        .projection = MEL_MAT4_IDENTITY,
    };

    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = S8("refresh_view"),
        .camera = &camera_a,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.1f, 0.2f, 0.3f, 1.0f),
    });
    mel_view_attach_source(view, source);

    Mel_Swapchain_Handle swapchain = make_mock_swapchain();
    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("refresh"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("refresh"));
    mel_frame_recipe_use_technique(recipe, view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(recipe, view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)1);

    mel_view_set_camera(view, &camera_b);
    mel_view_set_clear_color(view, mel_vec4(0.7f, 0.6f, 0.5f, 1.0f));
    mel_view_set_target_mode(view, MEL_VIEW_TARGET_FIT);
    mel_view_set_design_size(view, 320, 180);

    ok = mel_frame_plan_refresh(plan, .dev = &fake_dev);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(mel_frame_plan_dirty_flags(plan), (u32)MEL_FRAME_DIRTY_PARAMETERS);
    MEL_ASSERT(graph.passes.items[0].camera == &camera_b);
    MEL_ASSERT_EQ(graph.passes.items[0].viewport_mode, (u32)MEL_PASS_VIEWPORT_FIT);
    MEL_ASSERT_EQ(graph.passes.items[0].viewport_design_width, (u32)320);
    MEL_ASSERT_EQ(graph.passes.items[0].viewport_design_height, (u32)180);
    MEL_ASSERT_EQ(graph.passes.items[0].write_targets[0].clear.color.r, 0.7f);
    MEL_ASSERT_EQ(graph.passes.items[0].write_targets[0].clear.color.g, 0.6f);
    MEL_ASSERT_EQ(graph.passes.items[0].write_targets[0].clear.color.b, 0.5f);

    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(view);
    mel_source_destroy(source);
    mel_render_list_shutdown(&list);
}

MEL_TEST(frame_plan_refresh_reports_topology_dirty_when_view_sources_change, .tags = "render")
{
    Mel_Render_List list_a;
    mel_render_list_init(&list_a,
        .name = S8("sprites_a"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());
    Mel_Render_List list_b;
    mel_render_list_init(&list_b,
        .name = S8("sprites_b"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Source_Handle source_a = mel_source_from_render_list(&list_a, MEL_SCHEMA_SPRITE);
    Mel_Source_Handle source_b = mel_source_from_render_list(&list_b, MEL_SCHEMA_SPRITE);

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };

    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = S8("topology_view"),
        .camera = &camera,
        .clear_color_enabled = true,
    });
    mel_view_attach_source(view, source_a);

    Mel_Swapchain_Handle swapchain = make_mock_swapchain();
    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("topology"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("topology"));
    mel_frame_recipe_use_technique(recipe, view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(recipe, view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1);
    MEL_ASSERT(ok);

    mel_view_attach_source(view, source_b);
    ok = mel_frame_plan_refresh(plan, .dev = &fake_dev);
    MEL_ASSERT(!ok);
    MEL_ASSERT_EQ(mel_frame_plan_dirty_flags(plan), (u32)MEL_FRAME_DIRTY_TOPOLOGY);

    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(view);
    mel_source_destroy(source_b);
    mel_source_destroy(source_a);
    mel_render_list_shutdown(&list_b);
    mel_render_list_shutdown(&list_a);
}

MEL_TEST(frame_plan_refresh_reports_source_shape_dirty_when_retained_source_backing_changes, .tags = "render")
{
    Mel_Render_List list_a;
    mel_render_list_init(&list_a,
        .name = S8("retained_a"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_RETAINED,
        .alloc = mel_alloc_heap());
    Mel_Render_List list_b;
    mel_render_list_init(&list_b,
        .name = S8("retained_b"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_RETAINED,
        .alloc = mel_alloc_heap());

    Mel_Source_Handle source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("retained_source"),
        .kind = MEL_SOURCE_RETAINED,
        .schema = MEL_SCHEMA_SPRITE,
        .access_flags = MEL_SOURCE_ACCESS_CPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_RETAINED,
    });
    mel_source_set_render_list(source, &list_a);

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };

    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = S8("shape_view"),
        .camera = &camera,
        .clear_color_enabled = true,
    });
    mel_view_attach_source(view, source);

    Mel_Swapchain_Handle swapchain = make_mock_swapchain();
    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("shape"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("shape"));
    mel_frame_recipe_use_technique(recipe, view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(recipe, view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1);
    MEL_ASSERT(ok);

    mel_source_set_render_list(source, &list_b);
    ok = mel_frame_plan_refresh(plan, .dev = &fake_dev);
    MEL_ASSERT(!ok);
    MEL_ASSERT_EQ(mel_frame_plan_dirty_flags(plan), (u32)MEL_FRAME_DIRTY_SOURCE_SHAPE);

    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(view);
    mel_source_destroy(source);
    mel_render_list_shutdown(&list_b);
    mel_render_list_shutdown(&list_a);
}

MEL_TEST(frame_plan_orders_same_swapchain_views_by_explicit_binding_order, .tags = "render")
{
    Mel_Render_List world_list;
    mel_render_list_init(&world_list,
        .name = S8("world"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_List hud_list;
    mel_render_list_init(&hud_list,
        .name = S8("hud"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Source_Handle world_source = mel_source_from_render_list(&world_list, MEL_SCHEMA_SPRITE);
    Mel_Source_Handle hud_source = mel_source_from_render_list(&hud_list, MEL_SCHEMA_SPRITE);

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };

    Mel_View_Handle world_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("world"),
        .camera = &camera,
        .clear_color_enabled = true,
        .composition_mode = MEL_VIEW_COMPOSE_REPLACE,
    });
    mel_view_attach_source(world_view, world_source);

    Mel_View_Handle hud_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("hud"),
        .camera = &camera,
        .clear_color_enabled = true,
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
    });
    mel_view_attach_source(hud_view, hud_source);

    Mel_Swapchain_Handle swapchain = make_mock_swapchain();

    Mel_Frame_Recipe_Handle recipe = mel_frame_recipe_create(S8("ordered"));
    Mel_Frame_Plan_Handle plan = mel_frame_plan_create(S8("ordered"));
    mel_frame_recipe_use_technique(recipe, world_view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_use_technique(recipe, hud_view, MEL_TECHNIQUE_SPRITE);

    mel_frame_recipe_overlay_ordered(recipe, hud_view, swapchain, 20);
    mel_frame_recipe_present_ordered(recipe, world_view, swapchain, 10);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)2);
    MEL_ASSERT(graph.passes.items[0].read_lists != nullptr);
    MEL_ASSERT(graph.passes.items[1].read_lists != nullptr);
    MEL_ASSERT(graph.passes.items[0].read_lists[0] == &world_list);
    MEL_ASSERT(graph.passes.items[1].read_lists[0] == &hud_list);
    MEL_ASSERT_EQ(graph.passes.items[0].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_CLEAR);
    MEL_ASSERT_EQ(graph.passes.items[1].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_LOAD);

    mel_frame_recipe_present_ordered(recipe, world_view, swapchain, 30);
    mel_frame_recipe_overlay_ordered(recipe, hud_view, swapchain, 10);

    ok = mel_frame_plan_compile(plan, recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)2);
    MEL_ASSERT(graph.passes.items[0].read_lists[0] == &hud_list);
    MEL_ASSERT(graph.passes.items[1].read_lists[0] == &world_list);
    MEL_ASSERT_EQ(graph.passes.items[0].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_CLEAR);
    MEL_ASSERT_EQ(graph.passes.items[1].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_CLEAR);

    mel_render_graph_shutdown(&graph);
    mel_frame_plan_destroy(plan);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(hud_view);
    mel_view_destroy(world_view);
    mel_source_destroy(hud_source);
    mel_source_destroy(world_source);
    mel_render_list_shutdown(&hud_list);
    mel_render_list_shutdown(&world_list);
}
