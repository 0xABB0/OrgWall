#include "../melody/test.harness.h"
#include "../melody/render.stage.3d.h"
#include "../melody/render.frame_plan.h"
#include "../melody/render.camera.h"
#include "../melody/render.list.h"
#include "../melody/mesh.pass.h"
#include "../melody/sprite.pass.h"
#include "../melody/text.pass.h"
#include "../melody/swapchain.h"
#include "../melody/allocator.heap.h"
#include "../melody/math.mat4.h"
#include "../melody/string.str8.h"
#include "../melody/ui.widget.panel.h"
#include "../melody/ui.widget.button.h"

typedef struct {
    bool acquire_result;
    bool present_result;
} Stage3D_Mock_Swapchain;

static bool stage3d_mock_acquire(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)sc;
    (void)dev;
    return true;
}

static void stage3d_mock_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)sc;
    (void)dev;
}

static void stage3d_mock_resize(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height)
{
    (void)dev;
    sc->extent = (VkExtent2D){ width, height };
}

static void stage3d_mock_shutdown(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev;
    if (sc->data)
        mel_dealloc(mel_alloc_heap(), sc->data);
    sc->data = nullptr;
}

static const Mel_Swapchain_Vtable s_stage3d_mock_vtable = {
    .acquire = stage3d_mock_acquire,
    .present = stage3d_mock_present,
    .resize = stage3d_mock_resize,
    .shutdown = stage3d_mock_shutdown,
};

static Mel_Swapchain_Handle make_stage3d_mock_swapchain(void)
{
    Stage3D_Mock_Swapchain* mock = mel_alloc_type(mel_alloc_heap(), Stage3D_Mock_Swapchain);
    *mock = (Stage3D_Mock_Swapchain){0};

    Mel_Swapchain_Entry entry = {
        .swapchain = {
            .vtable = &s_stage3d_mock_vtable,
            .data = mock,
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .extent = { 960, 540 },
            .image_count = 2,
        },
    };

    return mel_swapchain_registry_insert(&entry);
}

MEL_TEST(render_stage_3d_builds_named_world_hud_layers_without_manual_recipe_wiring, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_stage3d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("mesh_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_List hud;
    mel_render_list_init(&hud,
        .name = S8("hud"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_List debug;
    mel_render_list_init(&debug,
        .name = S8("debug"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Stage_3D stage;
    bool ok = mel_render_stage_3d_init(&stage,
        .name = S8("stage3d"),
        .swapchain = swapchain,
        .world_camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)2,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)3,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    MEL_ASSERT(mel_view_handle_valid(mel_render_stage_3d_view(&stage, MEL_RENDER_STAGE_3D_LAYER_WORLD)));
    MEL_ASSERT(mel_view_handle_valid(mel_render_stage_3d_view(&stage, MEL_RENDER_STAGE_3D_LAYER_HUD)));
    MEL_ASSERT(mel_view_handle_valid(mel_render_stage_3d_view(&stage, MEL_RENDER_STAGE_3D_LAYER_DEBUG)));
    MEL_ASSERT(mel_view_handle_valid(mel_render_stage_3d_view(&stage, MEL_RENDER_STAGE_3D_LAYER_UI)));

    MEL_ASSERT(mel_render_stage_3d_attach_mesh_list(&stage, &world));
    MEL_ASSERT(mel_render_stage_3d_attach_sprite_list_to_layer(&stage, MEL_RENDER_STAGE_3D_LAYER_HUD, &hud));
    MEL_ASSERT(mel_render_stage_3d_attach_sprite_list_to_layer(&stage, MEL_RENDER_STAGE_3D_LAYER_DEBUG, &debug));
    ok = mel_render_stage_3d_rebuild(&stage);
    MEL_ASSERT(ok);

    Mel_Render_Graph* graph = mel_render_stage_3d_graph(&stage);
    MEL_ASSERT_EQ(graph->passes.count, (usize)3);
    MEL_ASSERT(graph->passes.items[0].read_lists[0] == &world);
    MEL_ASSERT(graph->passes.items[0].write_targets[1].target != nullptr);
    MEL_ASSERT(graph->passes.items[1].read_lists[0] == &hud);
    MEL_ASSERT(graph->passes.items[2].read_lists[0] == &debug);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(mel_render_stage_3d_plan(&stage)), (u32)3);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_stage_3d_plan(&stage), 0, &resolved));
    MEL_ASSERT(resolved.family == MEL_TECHNIQUE_MESH);
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_stage_3d_plan(&stage), 2, &resolved));
    MEL_ASSERT(resolved.family == MEL_TECHNIQUE_DEBUG);

    mel_render_stage_3d_shutdown(&stage);
    mel_render_list_shutdown(&debug);
    mel_render_list_shutdown(&hud);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

static void stage3d_test_widget_clicked(void* user_data)
{
    bool* clicked = user_data;
    *clicked = true;
}

MEL_TEST(render_stage_3d_ui_widget_layer_renders_on_ui_layer_and_processes_events, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_stage3d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Stage_3D stage;
    bool ok = mel_render_stage_3d_init(&stage,
        .name = S8("stage3d_ui"),
        .swapchain = swapchain,
        .world_camera = &camera,
        .clear_color_enabled = true,
        .dev = &fake_dev,
        .mesh_pass = (Mel_Mesh_Pass*)(uintptr_t)1,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)2,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)3,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_WPanel root;
    mel_wpanel_init(&root);
    mel_widget_set_position(&root.base, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&root.base, mel_vec2(100.0f, 100.0f));

    bool clicked = false;
    Mel_WButton button;
    mel_wbutton_init(&button);
    button.on_click = stage3d_test_widget_clicked;
    button.click_data = &clicked;
    mel_widget_set_position(&button.base, mel_vec2(10.0f, 10.0f));
    mel_widget_set_size(&button.base, mel_vec2(40.0f, 20.0f));
    mel_widget_add_child(&root.base, &button.base);

    Mel_Render_Stage_3D_Widget_Layer layer;
    ok = mel_render_stage_3d_widget_layer_init(&stage, &layer,
        .name = S8("ui_widgets"),
        .root = &root.base,
        .layer = MEL_RENDER_STAGE_3D_LAYER_UI,
        .input_scale_x = 2.0f,
        .input_scale_y = 2.0f,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    ok = mel_render_stage_3d_rebuild(&stage);
    MEL_ASSERT(ok);

    Mel_Render_Graph* graph = mel_render_stage_3d_graph(&stage);
    MEL_ASSERT_EQ(graph->passes.count, (usize)1);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(mel_render_stage_3d_plan(&stage)), (u32)1);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_stage_3d_plan(&stage), 0, &resolved));
    MEL_ASSERT(resolved.family == MEL_TECHNIQUE_UI);

    Mel_Render_List* list = mel_render_stage_3d_widget_layer_list(&layer);
    mel_render_list_begin_frame(list, 1);
    mel_render_list_produce(list);
    MEL_ASSERT_EQ(list->count, (u32)2);

    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 30.0f;
    event.motion.y = 30.0f;
    mel_render_stage_3d_widget_layer_process_event(&layer, &event);
    MEL_ASSERT(button.base.state & MEL_WIDGET_STATE_HOVERED);

    event = (SDL_Event){0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.x = 30.0f;
    event.button.y = 30.0f;
    event.button.button = 1;
    mel_render_stage_3d_widget_layer_process_event(&layer, &event);
    MEL_ASSERT(button.base.state & MEL_WIDGET_STATE_PRESSED);

    event = (SDL_Event){0};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.x = 30.0f;
    event.button.y = 30.0f;
    event.button.button = 1;
    mel_render_stage_3d_widget_layer_process_event(&layer, &event);
    MEL_ASSERT(clicked);

    mel_render_stage_3d_widget_layer_shutdown(&stage, &layer);
    mel_widget_destroy(&root.base);
    mel_render_stage_3d_shutdown(&stage);
    mel_swapchain_registry_remove(swapchain, nullptr);
}
