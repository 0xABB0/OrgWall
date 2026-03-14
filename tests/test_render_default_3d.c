#include "../melody/test.harness.h"
#include "../melody/render.default.3d.h"
#include "../melody/render.frame_plan.h"
#include "../melody/render.material.h"
#include "../melody/render.light.h"
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

static void default3d_mock_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)sc;
    (void)dev;
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

static const Mel_Vec3 s_test_mesh_positions[] = {
    { .x = -1.0f, .y = 0.0f, .z = 0.0f },
    { .x =  1.0f, .y = 0.0f, .z = 0.0f },
    { .x =  0.0f, .y = 1.0f, .z = 0.0f },
};

static const u32 s_test_mesh_indices[] = { 0, 1, 2 };

static const Mel_Mesh s_test_mesh = {
    .positions = s_test_mesh_positions,
    .colors = nullptr,
    .vertex_count = 3,
    .indices = s_test_mesh_indices,
    .index_count = 3,
};

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

static Mel_Technique_Policy_Result test_mesh_policy_prefer_forward(Mel_Frame_Plan_Technique_Ctx* ctx,
    const Mel_Technique_Desc* technique, void* user)
{
    (void)ctx;
    (void)user;
    if (str8_ieq(technique->name, S8("mesh.forward")))
    {
        return (Mel_Technique_Policy_Result){
            .allow = true,
            .priority_bias = 300,
            .kind = MEL_TECHNIQUE_CHECK_OK,
            .reason = S8("test policy prefers forward mesh path"),
        };
    }
    if (str8_ieq(technique->name, S8("mesh.draw_stream")))
    {
        return (Mel_Technique_Policy_Result){
            .allow = true,
            .priority_bias = -200,
            .kind = MEL_TECHNIQUE_CHECK_OK,
            .reason = S8("test policy de-prioritizes draw-stream mesh path"),
        };
    }

    return (Mel_Technique_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8(""),
    };
}

static Mel_Technique_Policy_Result test_mesh_policy_prefer_visibility(Mel_Frame_Plan_Technique_Ctx* ctx,
    const Mel_Technique_Desc* technique, void* user)
{
    (void)ctx;
    (void)user;
    if (str8_ieq(technique->name, S8("mesh.visibility_buffer")))
    {
        return (Mel_Technique_Policy_Result){
            .allow = true,
            .priority_bias = 500,
            .kind = MEL_TECHNIQUE_CHECK_OK,
            .reason = S8("test policy prefers visibility-buffer mesh path"),
        };
    }
    return (Mel_Technique_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8(""),
    };
}

static Mel_Technique_Policy_Result test_mesh_policy_prefer_deferred(Mel_Frame_Plan_Technique_Ctx* ctx,
    const Mel_Technique_Desc* technique, void* user)
{
    (void)ctx;
    (void)user;
    if (str8_ieq(technique->name, S8("mesh.deferred")))
    {
        return (Mel_Technique_Policy_Result){
            .allow = true,
            .priority_bias = 500,
            .kind = MEL_TECHNIQUE_CHECK_OK,
            .reason = S8("test policy prefers deferred mesh path"),
        };
    }
    return (Mel_Technique_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8(""),
    };
}

static Mel_Technique_Policy_Result test_mesh_policy_allow_default(Mel_Frame_Plan_Technique_Ctx* ctx,
    const Mel_Technique_Desc* technique, void* user)
{
    (void)ctx;
    (void)technique;
    (void)user;
    return (Mel_Technique_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8(""),
    };
}

static Mel_Material_Check_Result test_material_backend_support(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    (void)ctx;
    (void)material_template;
    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("test backend supported"),
    };
}

static Mel_Material_Check_Result test_material_backend_match_unlit_forward(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    if (!str8_ieq(ctx->technique_name, S8("mesh.forward")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_TECHNIQUE_MISMATCH,
            .reason = S8("test backend requires mesh.forward"),
        };
    }

    if (!str8_ieq(mel_material_template_profile(material_template), S8("surface.unlit")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_PROFILE_MISMATCH,
            .reason = S8("test backend expects surface.unlit"),
        };
    }

    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("test backend matches unlit forward path"),
    };
}

static Mel_Material_Policy_Result test_material_policy_prefer_debug_tint(Mel_Frame_Plan_Material_Ctx* ctx,
    const Mel_Material_Backend_Desc* backend, Mel_Material_Template_Handle material_template, void* user)
{
    (void)ctx;
    (void)material_template;
    (void)user;
    if (str8_ieq(backend->name, S8("surface.unlit.debug_tint")))
    {
        return (Mel_Material_Policy_Result){
            .allow = true,
            .priority_bias = 300,
            .kind = MEL_MATERIAL_CHECK_OK,
            .reason = S8("test policy prefers debug-tint material backend"),
        };
    }

    if (str8_ieq(backend->name, S8("surface.unlit.forward")))
    {
        return (Mel_Material_Policy_Result){
            .allow = true,
            .priority_bias = -150,
            .kind = MEL_MATERIAL_CHECK_OK,
            .reason = S8("test policy keeps stock backend as fallback"),
        };
    }

    return (Mel_Material_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8(""),
    };
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
    MEL_ASSERT_EQ(diagnostic_count, (u32)8);

    bool saw_mesh_shader = false;
    bool saw_compute_indirect_batch = false;
    bool saw_compute_indirect = false;
    bool saw_indirect = false;
    bool saw_deferred = false;
    bool saw_draw_stream = false;
    bool saw_forward = false;
    for (u32 i = 0; i < diagnostic_count; i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(mel_render_default_3d_plan(&renderer), i, &diag));
        if (str8_ieq(diag.technique_name, S8("mesh.mesh_shader")))
        {
            saw_mesh_shader = true;
            MEL_ASSERT(!diag.supported);
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT(str8_ieq(diag.reason, S8("mesh shader capability unavailable")));
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.compute_indirect_batch")))
        {
            saw_compute_indirect_batch = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT(str8_ieq(diag.reason, S8("no gpu batched compute-cull stream source attached")));
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.compute_indirect")))
        {
            saw_compute_indirect = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT(str8_ieq(diag.reason, S8("no gpu compute-cull stream source attached")));
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.indirect")))
        {
            saw_indirect = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT(str8_ieq(diag.reason, S8("no gpu indirect stream source attached")));
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.deferred")))
        {
            saw_deferred = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT(str8_ieq(diag.reason, S8("deferred gpu paths require a material-table source")));
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.draw_stream")))
        {
            saw_draw_stream = true;
            MEL_ASSERT(diag.matched);
            MEL_ASSERT(diag.selected);
            MEL_ASSERT(str8_ieq(diag.reason, S8("found gpu draw-stream source")));
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.forward")))
        {
            saw_forward = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT(str8_ieq(diag.reason, S8("no mesh-instance render-list source attached")));
        }
    }
    MEL_ASSERT(saw_mesh_shader);
    MEL_ASSERT(saw_compute_indirect_batch);
    MEL_ASSERT(saw_compute_indirect);
    MEL_ASSERT(saw_indirect);
    MEL_ASSERT(saw_deferred);
    MEL_ASSERT(saw_draw_stream);
    MEL_ASSERT(saw_forward);

    mel_source_destroy(source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_builds_mesh_pass_from_gpu_indirect_stream_source, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_gpu_indirect_stream"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Gpu_Buffer indirect_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Indirect_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .indirect_buffer = (VkBuffer)(uintptr_t)4,
        .draw_count = 2,
        .stride = MEL_MESH_INDIRECT_BATCH_COMMAND_STRIDE,
    };
    Mel_Source_Handle source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_indirect_stream"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_INDIRECT_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(source, &indirect_buffer);

    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), source));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)1);
    MEL_ASSERT(graph->passes.items[0].read_lists == nullptr);
    MEL_ASSERT(graph->passes.items[0].read_sources != nullptr);
    MEL_ASSERT(graph->passes.items[0].read_sources[0].handle.index == source.handle.index);
    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_default_3d_plan(&renderer), 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.indirect")));

    mel_source_destroy(source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_builds_compute_and_graphics_passes_from_gpu_cull_stream_source, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_gpu_cull_stream"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Gpu_Buffer cull_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Cull_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .indirect_buffer = (VkBuffer)(uintptr_t)4,
        .vertex_count = 24,
        .index_count = 36,
        .bounds_center = mel_vec3(0.0f, 0.0f, 0.0f),
        .bounds_radius = 1.9f,
    };
    Mel_Source_Handle source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_cull_stream"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_CULL_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(source, &cull_buffer);

    Mel_Material_Table table = {
        .dev = &fake_dev,
        .buffer = { .buffer = (VkBuffer)(uintptr_t)5 },
    };
    Mel_Source_Handle material_source = mel_source_from_material_table(&table);

    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), source));
    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), material_source));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)2);
    MEL_ASSERT_EQ(graph->passes.items[0].type, (u32)MEL_PASS_COMPUTE);
    MEL_ASSERT_EQ(graph->passes.items[1].type, (u32)MEL_PASS_GRAPHICS);
    MEL_ASSERT_EQ(graph->passes.items[1].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_CLEAR);
    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_default_3d_plan(&renderer), 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.compute_indirect")));

    mel_source_destroy(material_source);
    mel_source_destroy(source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_builds_compute_and_graphics_passes_from_gpu_cull_batch_stream_source, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_gpu_cull_batch_stream"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Gpu_Buffer indirect_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Cull_Batch_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .metadata_buffer = (VkBuffer)(uintptr_t)4,
        .indirect_buffer = (VkBuffer)(uintptr_t)5,
        .vertex_count = 24 * 4,
        .index_count = 36 * 4,
        .draw_count = 4,
        .stride = MEL_MESH_INDIRECT_BATCH_COMMAND_STRIDE,
    };
    Mel_Source_Handle source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_cull_batch_stream"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_CULL_BATCH_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(source, &indirect_buffer);

    Mel_Material_Table table = {
        .dev = &fake_dev,
        .buffer = { .buffer = (VkBuffer)(uintptr_t)6 },
    };
    Mel_Source_Handle material_source = mel_source_from_material_table(&table);

    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), source));
    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), material_source));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)2);
    MEL_ASSERT_EQ(graph->passes.items[0].type, (u32)MEL_PASS_COMPUTE);
    MEL_ASSERT_EQ(graph->passes.items[1].type, (u32)MEL_PASS_GRAPHICS);
    MEL_ASSERT_EQ(graph->passes.items[1].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_CLEAR);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_default_3d_plan(&renderer), 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.compute_indirect_batch")));

    mel_source_destroy(material_source);
    mel_source_destroy(source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_prefers_mesh_shader_variant_when_capability_and_sources_match, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};
    fake_dev.capabilities.mesh_shader = true;

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_mesh_shader"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Gpu_Buffer stream_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Draw_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .vertex_count = 24,
        .index_count = 36,
    };
    Mel_Source_Handle stream_source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_stream_mesh_shader"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(stream_source, &stream_buffer);

    Mel_Material_Table table = {
        .dev = &fake_dev,
        .buffer = { .buffer = (VkBuffer)(uintptr_t)4 },
    };
    Mel_Source_Handle material_source = mel_source_from_material_table(&table);

    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), stream_source));
    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), material_source));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.mesh_shader")));

    bool saw_mesh_shader = false;
    bool saw_draw_stream = false;
    for (u32 i = 0; i < mel_frame_plan_technique_diagnostic_count(plan); i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(plan, i, &diag));
        if (str8_ieq(diag.technique_name, S8("mesh.mesh_shader")))
        {
            saw_mesh_shader = true;
            MEL_ASSERT(diag.supported);
            MEL_ASSERT(diag.matched);
            MEL_ASSERT(diag.selected);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.draw_stream")))
        {
            saw_draw_stream = true;
            MEL_ASSERT(diag.matched);
            MEL_ASSERT(!diag.selected);
        }
    }
    MEL_ASSERT(saw_mesh_shader);
    MEL_ASSERT(saw_draw_stream);

    mel_source_destroy(material_source);
    mel_source_destroy(stream_source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_forwards_material_table_source_with_gpu_draw_stream, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_gpu_stream_material_table"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Gpu_Buffer stream_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Draw_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .index_count = 36,
    };
    Mel_Source_Handle stream_source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_stream"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(stream_source, &stream_buffer);

    Mel_Material_Table table = {
        .dev = &fake_dev,
        .buffer = { .buffer = (VkBuffer)(uintptr_t)4 },
    };
    Mel_Source_Handle material_source = mel_source_from_material_table(&table);

    Mel_View_Handle view = mel_render_default_3d_view(&renderer);
    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, view, stream_source));
    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, view, material_source));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)1);
    MEL_ASSERT(graph->passes.items[0].read_sources != nullptr);
    MEL_ASSERT(graph->passes.items[0].read_sources[0].handle.index == stream_source.handle.index);
    MEL_ASSERT(graph->passes.items[0].read_sources[1].handle.index == material_source.handle.index);
    MEL_ASSERT(!mel_source_handle_valid(graph->passes.items[0].read_sources[2]));

    mel_source_destroy(material_source);
    mel_source_destroy(stream_source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_both_mesh_source_shapes_compile_only_selected_variant_inputs, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_both_mesh_sources"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_both_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));
    Mel_Mesh_Entry* entry = mel_render_list_push(&world, mel_sort_key_mesh_opaque(0.0f));
    *entry = (Mel_Mesh_Entry){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
    };

    Mel_Gpu_Buffer stream_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Draw_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .index_count = 36,
    };
    Mel_Source_Handle stream_source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_stream_both"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(stream_source, &stream_buffer);
    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), stream_source));

    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_default_3d_plan(&renderer), 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.draw_stream")));

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)1);
    MEL_ASSERT(graph->passes.items[0].read_lists == nullptr);
    MEL_ASSERT(graph->passes.items[0].read_sources != nullptr);
    MEL_ASSERT(graph->passes.items[0].read_sources[0].handle.index == stream_source.handle.index);

    mel_source_destroy(stream_source);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
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
    MEL_ASSERT_EQ(diagnostic_count, (u32)9);

    bool saw_accel = false;
    bool saw_mesh_shader = false;
    bool saw_compute_indirect_batch = false;
    bool saw_compute_indirect = false;
    bool saw_indirect = false;
    bool saw_draw_stream = false;
    bool saw_forward = false;
    bool saw_deferred = false;
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
        else if (str8_ieq(diag.technique_name, S8("mesh.mesh_shader")))
        {
            saw_mesh_shader = true;
            MEL_ASSERT(!diag.supported);
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT_EQ(diag.reason_kind, (u32)MEL_TECHNIQUE_CHECK_CAPABILITY_UNAVAILABLE);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.compute_indirect_batch")))
        {
            saw_compute_indirect_batch = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT_EQ(diag.reason_kind, (u32)MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.compute_indirect")))
        {
            saw_compute_indirect = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT_EQ(diag.reason_kind, (u32)MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.indirect")))
        {
            saw_indirect = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT_EQ(diag.reason_kind, (u32)MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.draw_stream")))
        {
            saw_draw_stream = true;
            MEL_ASSERT(diag.supported);
            MEL_ASSERT(diag.matched);
            MEL_ASSERT(diag.selected);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.deferred")))
        {
            saw_deferred = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.forward")))
        {
            saw_forward = true;
            MEL_ASSERT(!diag.matched);
            MEL_ASSERT(!diag.selected);
        }
    }

    MEL_ASSERT(saw_accel);
    MEL_ASSERT(saw_mesh_shader);
    MEL_ASSERT(saw_compute_indirect_batch);
    MEL_ASSERT(saw_compute_indirect);
    MEL_ASSERT(saw_indirect);
    MEL_ASSERT(saw_draw_stream);
    MEL_ASSERT(saw_deferred);
    MEL_ASSERT(saw_forward);

    mel_source_destroy(source);
    mel_render_default_3d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_render_technique_unregister(MEL_TECHNIQUE_MESH, accel_name);
}

MEL_TEST(render_default_3d_family_policy_can_override_default_mesh_variant_choice, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_mesh_policy"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_policy_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));

    Mel_Gpu_Buffer gpu_buffer = { .buffer = (VkBuffer)(uintptr_t)1 };
    Mel_Mesh_Gpu_Draw_Stream stream = {
        .vertex_buffer = (VkBuffer)(uintptr_t)2,
        .index_buffer = (VkBuffer)(uintptr_t)3,
        .index_count = 36,
    };
    Mel_Source_Handle source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_stream_policy"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_GPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &stream,
    });
    mel_source_set_gpu_buffer(source, &gpu_buffer);
    MEL_ASSERT(mel_render_default_3d_attach_mesh_source_to_view(&renderer, mel_render_default_3d_view(&renderer), source));

    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_prefer_forward,
    });

    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.forward")));

    bool saw_forward = false;
    bool saw_draw_stream = false;
    bool saw_indirect = false;
    u32 diagnostic_count = mel_frame_plan_technique_diagnostic_count(plan);
    for (u32 i = 0; i < diagnostic_count; i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(plan, i, &diag));
        if (str8_ieq(diag.technique_name, S8("mesh.forward")))
        {
            saw_forward = true;
            MEL_ASSERT(diag.selected);
            MEL_ASSERT(str8_ieq(diag.reason, S8("test policy prefers forward mesh path")));
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.indirect")))
        {
            saw_indirect = true;
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT(!diag.matched);
        }
        else if (str8_ieq(diag.technique_name, S8("mesh.draw_stream")))
        {
            saw_draw_stream = true;
            MEL_ASSERT(!diag.selected);
            MEL_ASSERT_EQ(diag.reason_kind, (u32)MEL_TECHNIQUE_CHECK_POLICY_SKIPPED);
        }
    }
    MEL_ASSERT(saw_forward);
    MEL_ASSERT(saw_indirect);
    MEL_ASSERT(saw_draw_stream);

    mel_render_technique_clear_family_policy(MEL_TECHNIQUE_MESH);
    mel_source_destroy(source);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_resolves_surface_unlit_material_backend_for_mesh_entries, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_materials"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("material_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    MEL_ASSERT(mel_material_family_handle_valid(surface));
    Mel_Material_Template_Handle template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("test_surface_unlit"),
        .family = surface,
        .profile = S8("surface.unlit"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.8f, 0.6f, 0.4f, 1.0f),
    });
    Mel_Material_Instance_Handle instance = mel_material_instance_create(template);

    Mel_Mesh_Entry* entry = mel_render_list_push(&world, mel_sort_key_mesh_opaque(0.0f));
    *entry = (Mel_Mesh_Entry){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = instance,
    };

    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));
    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_material_count(plan), (u32)1);

    Mel_Frame_Plan_Resolved_Material resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_material_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.forward")));
    MEL_ASSERT(str8_ieq(resolved.backend_name, S8("surface.unlit.forward")));
    MEL_ASSERT(resolved.material_instance.handle.index == instance.handle.index);
    MEL_ASSERT(resolved.material_template.handle.index == template.handle.index);

    bool saw_selected_unlit = false;
    u32 diag_count = mel_frame_plan_material_diagnostic_count(plan);
    for (u32 i = 0; i < diag_count; i++)
    {
        Mel_Frame_Plan_Material_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_material_diagnostic_at(plan, i, &diag));
        if (!str8_ieq(diag.backend_name, S8("surface.unlit.forward")))
            continue;
        MEL_ASSERT(diag.supported);
        MEL_ASSERT(diag.matched);
        MEL_ASSERT(diag.selected);
        saw_selected_unlit = true;
    }
    MEL_ASSERT(saw_selected_unlit);

    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_material_instance_destroy(instance);
    mel_material_template_destroy(template);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_resolves_surface_standard_material_backend_for_mesh_entries, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Render_Default_3D renderer;
    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_surface_standard"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_world_surface_standard"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));

    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    Mel_Material_Template_Handle template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("test_surface_standard_template"),
        .family = surface,
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.90f, 0.85f, 0.80f, 1.0f),
    });
    Mel_Material_Instance_Handle instance = mel_material_instance_create(template);

    Mel_Mesh_Entry* entry = mel_render_list_push(&world, mel_sort_key_mesh_opaque(0.0f));
    *entry = (Mel_Mesh_Entry){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = instance,
    };

    ok = mel_render_default_3d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_material_count(plan), (u32)1);

    Mel_Frame_Plan_Resolved_Material resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_material_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.backend_name, S8("surface.standard.forward")));
    MEL_ASSERT(resolved.material_instance.handle.index == instance.handle.index);
    MEL_ASSERT(resolved.material_template.handle.index == template.handle.index);

    bool saw_selected_standard = false;
    u32 diag_count = mel_frame_plan_material_diagnostic_count(plan);
    for (u32 i = 0; i < diag_count; i++)
    {
        Mel_Frame_Plan_Material_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_material_diagnostic_at(plan, i, &diag));
        if (!str8_ieq(diag.backend_name, S8("surface.standard.forward")))
            continue;
        MEL_ASSERT(diag.supported);
        MEL_ASSERT(diag.matched);
        MEL_ASSERT(diag.selected);
        saw_selected_standard = true;
    }
    MEL_ASSERT(saw_selected_standard);

    mel_material_instance_destroy(instance);
    mel_material_template_destroy(template);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_material_family_policy_can_override_backend_choice, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_world_material_policy"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_material_policy"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));

    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    MEL_ASSERT(mel_material_family_handle_valid(surface));

    mel_material_backend_register(&(Mel_Material_Backend_Desc){
        .name = S8("surface.unlit.debug_tint"),
        .family = surface,
        .profile = S8("surface.unlit"),
        .technique_family = MEL_TECHNIQUE_MESH,
        .technique_name = S8("mesh.forward"),
        .priority = 50,
        .supports = test_material_backend_support,
        .matches = test_material_backend_match_unlit_forward,
    });
    mel_material_set_family_policy(&(Mel_Material_Family_Policy){
        .family = surface,
        .fn = test_material_policy_prefer_debug_tint,
        .user = nullptr,
    });

    Mel_Material_Template_Handle template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("policy_surface_template"),
        .family = surface,
        .profile = S8("surface.unlit"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
    });
    Mel_Material_Instance_Handle instance = mel_material_instance_create(template);

    mel_draw_mesh_opt(&world, (Mel_Draw_Mesh_Opt){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = instance,
    });

    ok = mel_render_default_3d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_material_count(plan), (u32)1);

    Mel_Frame_Plan_Resolved_Material resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_material_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.backend_name, S8("surface.unlit.debug_tint")));

    bool saw_selected_debug = false;
    bool saw_stock_fallback = false;
    u32 diag_count = mel_frame_plan_material_diagnostic_count(plan);
    for (u32 i = 0; i < diag_count; i++)
    {
        Mel_Frame_Plan_Material_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_material_diagnostic_at(plan, i, &diag));
        if (str8_ieq(diag.backend_name, S8("surface.unlit.debug_tint")))
        {
            MEL_ASSERT(diag.supported);
            MEL_ASSERT(diag.matched);
            MEL_ASSERT(diag.selected);
            saw_selected_debug = true;
        }
        if (str8_ieq(diag.backend_name, S8("surface.unlit.forward")))
        {
            MEL_ASSERT(diag.supported);
            MEL_ASSERT(diag.matched);
            MEL_ASSERT(!diag.selected);
            saw_stock_fallback = true;
        }
    }
    MEL_ASSERT(saw_selected_debug);
    MEL_ASSERT(saw_stock_fallback);

    mel_material_clear_family_policy(surface);
    mel_material_backend_unregister(surface, S8("surface.unlit.debug_tint"));
    mel_material_instance_destroy(instance);
    mel_material_template_destroy(template);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_can_build_visibility_buffer_branch, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_world_visibility"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_visibility"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.05f, 0.06f, 0.07f, 1.0f),
        .install_as_current_graph = false,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    MEL_ASSERT(mel_material_family_handle_valid(surface));
    Mel_Material_Template_Handle material_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("visibility_surface_template"),
        .family = surface,
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.7f, 0.8f, 0.9f, 1.0f),
        .params = (Mel_Material_Param_Desc[]){
            { .name = S8("roughness"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.35f },
            { .name = S8("metallic"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.1f },
            { .name = S8("lighting_weight"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 1.0f },
        },
        .param_count = 3,
    });
    Mel_Material_Instance_Handle material_instance = mel_material_instance_create(material_template);

    mel_draw_mesh_opt(&world, (Mel_Draw_Mesh_Opt){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = material_instance,
    });

    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));
    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_prefer_visibility,
        .user = nullptr,
    });

    ok = mel_render_default_3d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)3);

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(plan), (u32)1);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.visibility_buffer")));

    Mel_Frame_Plan_Resolved_Material resolved_material = {0};
    MEL_ASSERT_EQ(mel_frame_plan_resolved_material_count(plan), (u32)1);
    MEL_ASSERT(mel_frame_plan_resolved_material_at(plan, 0, &resolved_material));
    MEL_ASSERT(str8_ieq(resolved_material.backend_name, S8("surface.standard.visibility_buffer")));

    bool saw_visibility_selected = false;
    for (u32 i = 0; i < mel_frame_plan_technique_diagnostic_count(plan); i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(plan, i, &diag));
        if (str8_ieq(diag.technique_name, S8("mesh.visibility_buffer")))
        {
            MEL_ASSERT(diag.supported);
            MEL_ASSERT(diag.matched);
            MEL_ASSERT(diag.selected);
            saw_visibility_selected = true;
        }
    }
    MEL_ASSERT(saw_visibility_selected);

    mel_render_technique_clear_family_policy(MEL_TECHNIQUE_MESH);
    mel_material_instance_destroy(material_instance);
    mel_material_template_destroy(material_template);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_can_build_deferred_branch, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_deferred"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_deferred_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));

    Mel_Material_Template_Handle material_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("mesh_deferred_surface"),
        .family = mel_material_family_find(S8("surface")),
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.8f, 0.7f, 0.6f, 1.0f),
    });
    Mel_Material_Instance_Handle material = mel_material_instance_create(material_template);

    Mel_Mesh_Entry* entry = mel_render_list_push(&world, mel_sort_key_mesh_opaque(0.0f));
    *entry = (Mel_Mesh_Entry){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = material,
    };

    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_prefer_deferred,
    });

    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)3);
    MEL_ASSERT_EQ(graph->passes.items[0].type, (u32)MEL_PASS_GRAPHICS);
    MEL_ASSERT_EQ(graph->passes.items[1].type, (u32)MEL_PASS_COMPUTE);
    MEL_ASSERT_EQ(graph->passes.items[2].type, (u32)MEL_PASS_GRAPHICS);
    u32 fill_write_target_count = 0;
    for (Mel_Pass_Write_Target* wt = graph->passes.items[0].write_targets; wt && wt->target; wt++)
        fill_write_target_count++;
    MEL_ASSERT_EQ(fill_write_target_count, (u32)5);
    u32 resolve_read_target_count = 0;
    for (Mel_Render_Target** rt = graph->passes.items[2].read_targets; rt && *rt; rt++)
        resolve_read_target_count++;
    MEL_ASSERT_EQ(resolve_read_target_count, (u32)4);

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.deferred")));

    Mel_Frame_Plan_Resolved_Material resolved_material = {0};
    MEL_ASSERT(mel_frame_plan_resolved_material_at(plan, 0, &resolved_material));
    MEL_ASSERT(str8_ieq(resolved_material.backend_name, S8("surface.standard.deferred")));

    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_allow_default,
    });
    mel_material_instance_destroy(material);
    mel_material_template_destroy(material_template);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_deferred_branch_reads_clustered_light_source, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_deferred_clustered_light"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_deferred_light_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));

    Mel_Material_Template_Handle material_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("mesh_deferred_light_surface"),
        .family = mel_material_family_find(S8("surface")),
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.8f, 0.7f, 0.6f, 1.0f),
    });
    Mel_Material_Instance_Handle material = mel_material_instance_create(material_template);
    Mel_Mesh_Entry* entry = mel_render_list_push(&world, mel_sort_key_mesh_opaque(0.0f));
    *entry = (Mel_Mesh_Entry){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = material,
    };

    Mel_Light_Table light_table = {
        .dev = &fake_dev,
        .buffer = { .buffer = (VkBuffer)(uintptr_t)7 },
        .count = 4,
    };
    Mel_Source_Handle light_source = mel_source_from_light_table(&light_table);
    MEL_ASSERT(mel_render_default_3d_attach_light_source_to_view(&renderer, mel_render_default_3d_view(&renderer), light_source));

    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_prefer_deferred,
    });

    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Render_Graph* graph = mel_render_default_3d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)3);
    MEL_ASSERT_EQ(graph->passes.items[1].type, (u32)MEL_PASS_COMPUTE);
    MEL_ASSERT(graph->passes.items[1].read_sources != nullptr);
    MEL_ASSERT_EQ(mel_source_schema(graph->passes.items[1].read_sources[0]), (u32)MEL_SCHEMA_LIGHT);
    MEL_ASSERT(graph->passes.items[2].read_sources != nullptr);
    MEL_ASSERT_EQ(mel_source_schema(graph->passes.items[2].read_sources[0]), (u32)MEL_SCHEMA_LIGHT);

    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_allow_default,
    });
    mel_source_destroy(light_source);
    mel_material_instance_destroy(material);
    mel_material_template_destroy(material_template);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_visibility_buffer_falls_back_for_transparent_materials, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_world_visibility_transparent"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_visibility_transparent"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    MEL_ASSERT(mel_material_family_handle_valid(surface));
    Mel_Material_Template_Handle material_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("visibility_surface_transparent"),
        .family = surface,
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_TRANSPARENT,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.7f, 0.8f, 0.9f, 0.6f),
    });
    Mel_Material_Instance_Handle material_instance = mel_material_instance_create(material_template);

    mel_draw_mesh_opt(&world, (Mel_Draw_Mesh_Opt){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = material_instance,
    });

    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));
    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_prefer_visibility,
        .user = nullptr,
    });

    ok = mel_render_default_3d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(plan), (u32)1);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.forward")));

    Mel_Frame_Plan_Resolved_Material resolved_material = {0};
    MEL_ASSERT_EQ(mel_frame_plan_resolved_material_count(plan), (u32)1);
    MEL_ASSERT(mel_frame_plan_resolved_material_at(plan, 0, &resolved_material));
    MEL_ASSERT(str8_ieq(resolved_material.backend_name, S8("surface.standard.forward")));

    bool saw_visibility_rejected = false;
    for (u32 i = 0; i < mel_frame_plan_technique_diagnostic_count(plan); i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(plan, i, &diag));
        if (!str8_ieq(diag.technique_name, S8("mesh.visibility_buffer")))
            continue;
        MEL_ASSERT(diag.supported);
        MEL_ASSERT(!diag.matched);
        MEL_ASSERT(!diag.selected);
        MEL_ASSERT(diag.reason_kind == MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH);
        MEL_ASSERT(str8_contains(diag.reason, S8("opaque")));
        saw_visibility_rejected = true;
    }
    MEL_ASSERT(saw_visibility_rejected);

    mel_render_technique_clear_family_policy(MEL_TECHNIQUE_MESH);
    mel_material_instance_destroy(material_instance);
    mel_material_template_destroy(material_template);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_3d_deferred_falls_back_for_transparent_materials, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_3D renderer;
    bool ok = mel_render_default_3d_init(&renderer,
        .name = S8("default3d_deferred_transparent"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_deferred_transparent_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    MEL_ASSERT(mel_render_default_3d_attach_mesh_list(&renderer, &world));

    Mel_Material_Template_Handle material_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("mesh_deferred_transparent_surface"),
        .family = mel_material_family_find(S8("surface")),
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_TRANSPARENT,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.8f, 0.7f, 0.6f, 0.5f),
    });
    Mel_Material_Instance_Handle material = mel_material_instance_create(material_template);

    Mel_Mesh_Entry* entry = mel_render_list_push(&world, mel_sort_key_mesh_opaque(0.0f));
    *entry = (Mel_Mesh_Entry){
        .mesh = &s_test_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = material,
    };

    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_prefer_deferred,
    });

    MEL_ASSERT(mel_render_default_3d_rebuild(&renderer));

    Mel_Frame_Plan_Handle plan = mel_render_default_3d_plan(&renderer);
    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.technique_name, S8("mesh.forward")));

    Mel_Frame_Plan_Resolved_Material resolved_material = {0};
    MEL_ASSERT(mel_frame_plan_resolved_material_at(plan, 0, &resolved_material));
    MEL_ASSERT(str8_ieq(resolved_material.backend_name, S8("surface.standard.forward")));

    bool saw_deferred_diag = false;
    for (u32 i = 0; i < mel_frame_plan_technique_diagnostic_count(plan); i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        MEL_ASSERT(mel_frame_plan_technique_diagnostic_at(plan, i, &diag));
        if (!str8_ieq(diag.technique_name, S8("mesh.deferred")))
            continue;
        saw_deferred_diag = true;
        MEL_ASSERT(!diag.selected);
        MEL_ASSERT_EQ(diag.reason_kind, (u32)MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH);
    }
    MEL_ASSERT(saw_deferred_diag);

    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = test_mesh_policy_allow_default,
    });
    mel_material_instance_destroy(material);
    mel_material_template_destroy(material_template);
    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}
