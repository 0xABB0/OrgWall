#include "../melody/test.harness.h"
#include "../melody/render.source.h"
#include "../melody/render.view.h"
#include "../melody/render.frame_recipe.h"
#include "../melody/render.graph.h"
#include "../melody/render.list.h"
#include "../melody/render.camera.h"
#include "../melody/sprite.pass.h"
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
    mel_frame_recipe_use_technique(recipe, view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(recipe, view, swapchain);

    Mel_Render_Graph graph;
    mel_render_graph_init(&graph, .alloc = mel_alloc_heap());

    Mel_Gpu_Device fake_dev = {0};
    bool ok = mel_frame_recipe_compile(recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)1);
    MEL_ASSERT_EQ(graph.sorted_order.count, (usize)1);
    MEL_ASSERT(graph.passes.items[0].read_lists != nullptr);
    MEL_ASSERT(graph.passes.items[0].read_lists[0] == &list);

    ok = mel_frame_recipe_compile(recipe,
        .graph = &graph,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(graph.passes.count, (usize)1);

    mel_render_graph_shutdown(&graph);
    mel_frame_recipe_destroy(recipe);
    mel_swapchain_registry_remove(swapchain, nullptr);
    mel_view_destroy(view);
    mel_source_destroy(source);
    mel_render_list_shutdown(&list);
}
