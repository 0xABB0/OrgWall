#include "../melody/test.harness.h"
#include "../melody/render.default.2d.h"
#include "../melody/render.frame_plan.h"
#include "../melody/render.material.h"
#include "../melody/render.list.h"
#include "../melody/render.view.h"
#include "../melody/render.camera.h"
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
} Default2D_Mock_Swapchain;

static bool default2d_mock_acquire(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)sc;
    (void)dev;
    return true;
}

static bool default2d_mock_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev, VkCommandBuffer cmd, VkFence fence)
{
    (void)sc;
    (void)dev;
    (void)cmd;
    (void)fence;
    return true;
}

static void default2d_mock_resize(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height)
{
    (void)dev;
    sc->extent = (VkExtent2D){ width, height };
}

static void default2d_mock_shutdown(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev;
    if (sc->data)
        mel_dealloc(mel_alloc_heap(), sc->data);
    sc->data = nullptr;
}

static const Mel_Swapchain_Vtable s_default2d_mock_vtable = {
    .acquire = default2d_mock_acquire,
    .present = default2d_mock_present,
    .resize = default2d_mock_resize,
    .shutdown = default2d_mock_shutdown,
};

static Mel_Swapchain_Handle make_default2d_mock_swapchain(void)
{
    Default2D_Mock_Swapchain* mock = mel_alloc_type(mel_alloc_heap(), Default2D_Mock_Swapchain);
    *mock = (Default2D_Mock_Swapchain){0};

    Mel_Swapchain_Entry entry = {
        .swapchain = {
            .vtable = &s_default2d_mock_vtable,
            .data = mock,
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .extent = { 640, 480 },
            .image_count = 2,
        },
    };

    return mel_swapchain_registry_insert(&entry);
}

MEL_TEST(render_default_2d_builds_single_sprite_pass_from_attached_lists, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default2d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("world"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_List hud;
    mel_render_list_init(&hud,
        .name = S8("hud"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_2D renderer;
    bool ok = mel_render_default_2d_init(&renderer,
        .name = S8("default2d"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .install_as_current_graph = false,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    MEL_ASSERT(mel_render_default_2d_attach_sprite_list(&renderer, &world));
    MEL_ASSERT(mel_render_default_2d_attach_sprite_list(&renderer, &hud));
    MEL_ASSERT(mel_render_default_2d_attach_sprite_list(&renderer, &world));

    ok = mel_render_default_2d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Render_Graph* graph = mel_render_default_2d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)1);
    MEL_ASSERT(graph->passes.items[0].read_lists != nullptr);
    MEL_ASSERT(graph->passes.items[0].read_lists[0] == &world);
    MEL_ASSERT(graph->passes.items[0].read_lists[1] == &hud);
    MEL_ASSERT_NULL(graph->passes.items[0].read_lists[2]);

    mel_render_default_2d_shutdown(&renderer);
    mel_render_list_shutdown(&hud);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_2d_supports_extra_overlay_views_and_exposes_primitives, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default2d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("world"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Render_List hud;
    mel_render_list_init(&hud,
        .name = S8("hud"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera world_camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Camera hud_camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_2D renderer;
    bool ok = mel_render_default_2d_init(&renderer,
        .name = S8("open2d"),
        .swapchain = swapchain,
        .camera = &world_camera,
        .clear_color_enabled = true,
        .install_as_current_graph = false,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_View_Handle hud_view = mel_render_default_2d_add_view(&renderer,
        .name = S8("hud"),
        .camera = &hud_camera,
        .clear_color_enabled = false,
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
        .overlay = true,
        .order = 10);
    MEL_ASSERT(mel_view_handle_valid(hud_view));

    MEL_ASSERT(mel_frame_recipe_handle_valid(mel_render_default_2d_recipe(&renderer)));
    MEL_ASSERT(mel_frame_plan_handle_valid(mel_render_default_2d_plan(&renderer)));
    MEL_ASSERT(mel_view_handle_valid(mel_render_default_2d_view(&renderer)));
    MEL_ASSERT(mel_render_default_2d_attach_sprite_list(&renderer, &world));
    MEL_ASSERT(mel_render_default_2d_attach_sprite_list_to_view(&renderer, hud_view, &hud));

    ok = mel_render_default_2d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Render_Graph* graph = mel_render_default_2d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)2);
    MEL_ASSERT(graph->passes.items[0].read_lists[0] == &world);
    MEL_ASSERT(graph->passes.items[1].read_lists[0] == &hud);
    MEL_ASSERT_EQ(graph->passes.items[0].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_CLEAR);
    MEL_ASSERT_EQ(graph->passes.items[1].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_LOAD);

    mel_render_default_2d_shutdown(&renderer);
    mel_render_list_shutdown(&hud);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_2d_can_append_imgui_pass_after_compiled_views, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default2d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("world"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_2D renderer;
    bool ok = mel_render_default_2d_init(&renderer,
        .name = S8("imgui2d"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .enable_imgui = true,
        .install_as_current_graph = false,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);
    MEL_ASSERT(mel_render_default_2d_attach_sprite_list(&renderer, &world));

    ok = mel_render_default_2d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Render_Graph* graph = mel_render_default_2d_graph(&renderer);
    MEL_ASSERT_EQ(graph->passes.count, (usize)2);
    MEL_ASSERT(graph->passes.items[0].read_lists[0] == &world);
    MEL_ASSERT_NULL(graph->passes.items[1].read_lists);
    MEL_ASSERT_EQ(graph->passes.items[1].write_targets[0].load_op, (VkAttachmentLoadOp)VK_ATTACHMENT_LOAD_OP_LOAD);
    MEL_ASSERT(mel_render_default_2d_target(&renderer) == graph->passes.items[1].write_targets[0].target);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(mel_render_default_2d_plan(&renderer)), (u32)2);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_default_2d_plan(&renderer), 1, &resolved));
    MEL_ASSERT(resolved.family == MEL_TECHNIQUE_IMGUI);

    mel_render_default_2d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

MEL_TEST(render_default_2d_resolves_sprite_unlit_material_backend_for_sprite_entries, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default2d_mock_swapchain();

    Mel_Render_List world;
    mel_render_list_init(&world,
        .name = S8("world_material"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_2D renderer;
    bool ok = mel_render_default_2d_init(&renderer,
        .name = S8("default2d_material"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .install_as_current_graph = false,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_Material_Family_Handle sprite = mel_material_family_find(S8("sprite"));
    MEL_ASSERT(mel_material_family_handle_valid(sprite));

    Mel_Material_Template_Handle template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("test_sprite_template"),
        .family = sprite,
        .profile = S8("sprite.unlit"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.80f, 0.70f, 0.60f, 1.0f),
    });
    Mel_Material_Instance_Handle instance = mel_material_instance_create(template);

    mel_draw_sprite(&world,
        .pos = mel_vec2(10.0f, 20.0f),
        .size = mel_vec2(40.0f, 30.0f),
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = instance);

    MEL_ASSERT(mel_render_default_2d_attach_sprite_list(&renderer, &world));

    ok = mel_render_default_2d_rebuild(&renderer);
    MEL_ASSERT(ok);

    Mel_Frame_Plan_Handle plan = mel_render_default_2d_plan(&renderer);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_material_count(plan), (u32)1);

    Mel_Frame_Plan_Resolved_Material resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_material_at(plan, 0, &resolved));
    MEL_ASSERT(str8_ieq(resolved.backend_name, S8("sprite.unlit.sprite")));
    MEL_ASSERT(resolved.material_instance.handle.index == instance.handle.index);
    MEL_ASSERT(resolved.material_template.handle.index == template.handle.index);

    Mel_Frame_Plan_Material_Diagnostic diag = {0};
    MEL_ASSERT_EQ(mel_frame_plan_material_diagnostic_count(plan), (u32)1);
    MEL_ASSERT(mel_frame_plan_material_diagnostic_at(plan, 0, &diag));
    MEL_ASSERT(diag.supported);
    MEL_ASSERT(diag.matched);
    MEL_ASSERT(diag.selected);
    MEL_ASSERT(str8_ieq(diag.backend_name, S8("sprite.unlit.sprite")));

    mel_material_instance_destroy(instance);
    mel_material_template_destroy(template);
    mel_render_default_2d_shutdown(&renderer);
    mel_render_list_shutdown(&world);
    mel_swapchain_registry_remove(swapchain, nullptr);
}

static void test_widget_clicked(void* user_data)
{
    bool* clicked = user_data;
    *clicked = true;
}

MEL_TEST(render_default_2d_widget_layer_renders_and_processes_mouse_events, .tags = "render")
{
    Mel_Swapchain_Handle swapchain = make_default2d_mock_swapchain();

    Mel_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
    };
    Mel_Gpu_Device fake_dev = {0};

    Mel_Render_Default_2D renderer;
    bool ok = mel_render_default_2d_init(&renderer,
        .name = S8("widgets2d"),
        .swapchain = swapchain,
        .camera = &camera,
        .clear_color_enabled = true,
        .install_as_current_graph = false,
        .dev = &fake_dev,
        .sprite_pass = (Mel_Sprite_Pass*)(uintptr_t)1,
        .text_pass = (Mel_Text_Pass*)(uintptr_t)2,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    Mel_WPanel root;
    mel_wpanel_init(&root);
    mel_widget_set_position(&root.base, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&root.base, mel_vec2(100.0f, 100.0f));

    bool clicked = false;
    Mel_WButton button;
    mel_wbutton_init(&button);
    button.on_click = test_widget_clicked;
    button.click_data = &clicked;
    mel_widget_set_position(&button.base, mel_vec2(10.0f, 10.0f));
    mel_widget_set_size(&button.base, mel_vec2(40.0f, 20.0f));
    mel_widget_add_child(&root.base, &button.base);

    Mel_Render_Default_2D_Widget_Layer layer;
    ok = mel_render_default_2d_widget_layer_init(&renderer, &layer,
        .name = S8("widgets"),
        .root = &root.base,
        .camera = &camera,
        .overlay = true,
        .order = 10,
        .input_scale_x = 2.0f,
        .input_scale_y = 2.0f,
        .alloc = mel_alloc_heap());
    MEL_ASSERT(ok);

    ok = mel_render_default_2d_rebuild(&renderer);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(mel_frame_plan_resolved_technique_count(mel_render_default_2d_plan(&renderer)), (u32)1);

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    MEL_ASSERT(mel_frame_plan_resolved_technique_at(mel_render_default_2d_plan(&renderer), 0, &resolved));
    MEL_ASSERT(resolved.family == MEL_TECHNIQUE_UI);

    Mel_Render_List* list = mel_render_default_2d_widget_layer_list(&layer);
    mel_render_list_begin_frame(list, 1);
    mel_render_list_produce(list);
    MEL_ASSERT_EQ(list->count, (u32)2);

    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 30.0f;
    event.motion.y = 30.0f;
    mel_render_default_2d_widget_layer_process_event(&layer, &event);
    MEL_ASSERT(button.base.state & MEL_WIDGET_STATE_HOVERED);

    event = (SDL_Event){0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.x = 30.0f;
    event.button.y = 30.0f;
    event.button.button = 1;
    mel_render_default_2d_widget_layer_process_event(&layer, &event);
    MEL_ASSERT(button.base.state & MEL_WIDGET_STATE_PRESSED);

    event = (SDL_Event){0};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.x = 30.0f;
    event.button.y = 30.0f;
    event.button.button = 1;
    mel_render_default_2d_widget_layer_process_event(&layer, &event);
    MEL_ASSERT(clicked);

    mel_render_default_2d_widget_layer_shutdown(&renderer, &layer);
    mel_widget_destroy(&root.base);
    mel_render_default_2d_shutdown(&renderer);
    mel_swapchain_registry_remove(swapchain, nullptr);
}
