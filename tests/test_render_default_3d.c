#include "../melody/test.harness.h"
#include "../melody/render.default.3d.h"
#include "../melody/render.frame_plan.h"
#include "../melody/render.source.h"
#include "../melody/render.list.h"
#include "../melody/render.camera.h"
#include "../melody/render.target.h"
#include "../melody/mesh.pass.h"
#include "../melody/gpu.buffer.h"
#include "../melody/swapchain.h"
#include "../melody/allocator.heap.h"
#include "../melody/math.mat4.h"
#include "../melody/string.str8.h"

typedef struct {
    bool acquire_result;
    bool present_result;
} Default3D_Mock_Swapchain;

static bool default3d_mock_acquire(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)sc;
    (void)dev;
    return true;
}

static bool default3d_mock_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev, VkCommandBuffer cmd, VkFence fence)
{
    (void)sc;
    (void)dev;
    (void)cmd;
    (void)fence;
    return true;
}

static void default3d_mock_resize(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height)
{
    (void)dev;
    sc->extent = (VkExtent2D){ width, height };
}

static void default3d_mock_shutdown(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev;
    if (sc->data)
        mel_dealloc(mel_alloc_heap(), sc->data);
    sc->data = nullptr;
}

static const Mel_Swapchain_Vtable s_default3d_mock_vtable = {
    .acquire = default3d_mock_acquire,
    .present = default3d_mock_present,
    .resize = default3d_mock_resize,
    .shutdown = default3d_mock_shutdown,
};

static Mel_Swapchain_Handle make_default3d_mock_swapchain(void)
{
    Default3D_Mock_Swapchain* mock = mel_alloc_type(mel_alloc_heap(), Default3D_Mock_Swapchain);
    *mock = (Default3D_Mock_Swapchain){0};

    Mel_Swapchain_Entry entry = {
        .swapchain = {
            .vtable = &s_default3d_mock_vtable,
            .data = mock,
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .extent = { 960, 540 },
            .image_count = 2,
        },
    };

    return mel_swapchain_registry_insert(&entry);
}

static void test_mesh_variant_pass(Mel_Render_Pass_Ctx* ctx)
{
    (void)ctx;
}

static Mel_Technique_Check_Result test_mesh_variant_support_accel(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    bool ok = ctx->dev &&
        ctx->dev->capabilities.buffer_device_address &&
        !ctx->dev->capabilities.portability_subset;
    return (Mel_Technique_Check_Result){
        .ok = ok,
        .kind = ok ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_CAPABILITY_UNAVAILABLE,
        .reason = ok
            ? S8("game accel path enabled by device capabilities")
            : S8("game accel path requires non-portability gpu-addressable buffers"),
    };
}

static Mel_Technique_Check_Result test_mesh_variant_match_draw_stream(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Source_Handle* read_sources = mel_frame_plan_collect_sources(ctx->plan,
        ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_DRAW_STREAM);
    bool matched = read_sources != nullptr;
    mel_frame_plan_free_read_sources(ctx->plan, read_sources);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("game accel path found gpu draw-stream source") : S8("game accel path needs a gpu draw-stream source"),
    };
}

static Mel_Technique_Compile_Result test_mesh_variant_compile(const Mel_Technique_Compile_Ctx* ctx)
{
    bool ok = mel_frame_plan_add_pass(ctx->plan_ctx, ctx->technique->name,
        test_mesh_variant_pass, nullptr, nullptr, nullptr, nullptr);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

MEL_TEST(render_default_3d_builds_single_mesh_pass_with_depth_target, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.1f, 0.2f, 0.3f, 1.0f),
        .install_as_current_graph = false,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));

    ok = mel_render_default_3d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)1);
    MEL_ASSERT(graph->passes.items[0].read_lists[0] == &world);
    MEL_ASSERT(graph->passes.items[0].write_targets[0].target == mel_render_default_3d_target(&renderer));
    MEL_ASSERT_EQ(graph->passes.items[0].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_CLEAR);
    MEL_ASSERT(graph->passes.items[0].write_targets[1].target != nullptr);
    MEL_ASSERT_EQ(graph->passes.items[0].write_targets[1].target->kind, (u32)MEL_RENDER_TARGET_DEPTH);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(mel_render_default_3d_plan(&renderer)), (u32)1);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_default_3d_plan(&renderer), 0, &resolved));
    MEL_ASSERT(resolved.family == MEL_TECHNIQUE_MESH);
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.forward")));

    bool saw_swapchain_barrier = false;
    for (usize i = 0; i < graph->barriers.count; i++)
    {
        Mel_Render_Graph_Barrier* barrier = &graph->barriers.items[i];
        if (barrier->target == mel_render_default_3d_target(&renderer))
        {
            saw_swapchain_barrier = true;
            MEL_ASSERT_EQ(barrier->old_layout, (VkImageLayout)VK_IMAGE_LAYOUT_UNDEFINED);
            MEL_ASSERT_EQ(barrier->new_layout, (VkImageLayout)VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            break;
        }
    }
    MEL_ASSERT(saw_swapchain_barrier);

    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_refresh_resizes_generated_depth_target_without_recompile, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_world_refresh"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_refresh"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);
    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Render_Target* depth = mel_frame_plan_swapchain_depth_target(mel_render_default_3d_plan(&renderer), swapchain);
    MEL_ASSERT(depth != nullptr);
    MEL_ASSERT_EQ(depth->width, (u32)960);
    MEL_ASSERT_EQ(depth->height, (u32)540);

    Mel_Swapchain* sc = &mel_swapchain_registry_get(swapchain)->swapchain;
    mel_swapchain_resize(sc, &fake_dev, 1280, 720);

    MEL_ASSERT(mel_render_default_3d_refresh(&renderer));
    MEL_ASSERT(depth == mel_frame_plan_swapchain_depth_target(mel_render_default_3d_plan(&renderer), swapchain));
    MEL_ASSERT_EQ(depth->width, (u32)1280);
    MEL_ASSERT_EQ(depth->height, (u32)720);
    MEL_ASSERT_EQ(mel_render_default_3d_graph(&renderer)->passes.count, (usize)1);

    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_builds_mesh_pass_from_gpu_draw_stream_source, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_gpu_stream"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Gpu_Buffer gpu_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Draw_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .index_count = 36,
    };
    Mel_Source_Handle source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_stream"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(source, &gpu_buffer);

    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), source));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)1);
    MEL_ASSERT(graph->passes.items[0].read_lists == nullptr);
    MEL_ASSERT(graph->passes.items[0].read_sources != nullptr);
    MEL_ASSERT(graph->passes.items[0].read_sources[0].handle.index == source.handle.index);
    MEL_ASSERT(graph->passes.items[0].read_sources[0].handle.generation == source.handle.generation);
    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_default_3d_plan(&renderer), 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.draw_stream")));

    u32 diagnostic_count = mel_frame_plan_technique_diagnostic_count(mel_render_default_3d_plan(&renderer));
    MEL_ASSERT_EQ(diagnostic_count, (u32)2);

    Mel_Frame_Plan_Technique_Diagnostic first = {0};
    Mel_Frame_Plan_Technique_Diagnostic second = {0};
    MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(mel_render_default_3d_plan(&renderer), 0, &first));
    MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(mel_render_default_3d_plan(&renderer), 1, &second));
    MEL_ASSERT(str8_ieq(first.technique_name, S8("mesh.draw_stream")));
    MEL_ASSERT(first.matched);
    MEL_ASSERT(first.selected);
    MEL_ASSERT(str8_ieq(first.reason, S8("found gpu draw-stream source")));
    MEL_ASSERT(str8_ieq(second.technique_name, S8("mesh.forward")));
    MEL_ASSERT(!second.matched);
    MEL_ASSERT(!second.selected);
    MEL_ASSERT(str8_ieq(second.reason, S8("no mesh-instance render-list source attached")));

    mel_source_destroy(source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_allows_game_variant_to_extend_mesh_family_with_capability_fallback, .tags = "render")
{
    const str8 accel_name = S8("game.mesh.accel");
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = accel_name,
        .source_schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .priority = 300,
        .supports = test_mesh_variant_support_accel,
        .matches = test_mesh_variant_match_draw_stream,
        .compile = test_mesh_variant_compile,
    });

    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();
    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};
    fake_dev.capabilities.buffer_device_address = false;
    fake_dev.capabilities.portability_subset = true;

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_game_variant"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Gpu_Buffer gpu_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Draw_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .index_count = 36,
    };
    Mel_Source_Handle source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_stream_game_variant"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(source, &gpu_buffer);

    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), source));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.draw_stream")));

    u32 diagnostic_count = mel_frame_plan_technique_diagnostic_count(plan);
    MEL_ASSERT_EQ(diagnostic_count, (u32)3);

    bool saw_accel = false;
    bool saw_draw_stream = false;
    bool saw_forward = false;
    for (u32 i = 0; i < diagnostic_count; i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(plan, i, &diag));

        if (str8_ieq(diag.technique_name, accel_name))
        {
            saw_accel = true;
            MEL_ASSERT(!diag.supported);
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT_EQ(diag.reason_kind, (u32)MEL_TECHNIQUE_CHECK_CAPABILITY_UNAVAILABLE);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.draw_stream")))
        {
            saw_draw_stream = true;
            MEL_ASSERT(diag.supported);
            MEL_ASSERT(diag.matched);
            MEL_ASSERT(diag.selected);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.forward")))
        {
            saw_forward = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
        }
    }

    MEL_ASSERT(saw_accel);
    MEL_ASSERT(saw_draw_stream);
    MEL_ASSERT(saw_forward);

    mel_source_destroy(source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_render_technique_unregister(MEL_TECHNIQUE_MESH, accel_name);
}
