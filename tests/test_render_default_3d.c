#include "../melody/test.harness.h"
#include "../melody/render.default.3d.h"
#include "../melody/render.frame_plan.h"
#include "../melody/render.list.h"
#include "../melody/render.camera.h"
#include "../melody/render.target.h"
#include "../melody/mesh.pass.h"
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

    mel_render_default_3d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}
