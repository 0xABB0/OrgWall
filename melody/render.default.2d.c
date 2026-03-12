#include "render.default.2d.h"
#include "render.view.h"
#include "render.frame_recipe.h"
#include "render.frame_plan.h"
#include "render.source.h"
#include "sprite.pass.h"
#include "text.pass.h"
#include "core.engine.h"
#include "gpu.device.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "ui.widget.h"
#include "math.vec2.h"

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

static void mel__render_default_2d_imgui_pass(Mel_Render_Pass_Ctx* ctx)
{
    igRender();
    ImDrawData* draw_data = igGetDrawData();
    if (draw_data && draw_data->CmdListsCount > 0)
        ImGui_ImplVulkan_RenderDrawData(draw_data, ctx->cmd.cmd, VK_NULL_HANDLE);
}

static void mel__render_default_2d_widget_producer(Mel_Render_List* list, void* user)
{
    Mel_Render_Default_2D_Widget_Layer* layer = user;
    if (layer->root)
        mel_widget_draw(layer->root, list);
}

static void mel__render_default_2d_clear_extra_passes(Mel_Render_Default_2D* renderer)
{
    if (renderer->imgui_pass_name.data && renderer->graph.alloc)
    {
        mel_render_graph_remove_pass(&renderer->graph, renderer->imgui_pass_name);
        mel_dealloc(renderer->alloc, renderer->imgui_pass_name.data);
        renderer->imgui_pass_name = (str8){0};
    }
}

bool mel_render_default_2d_init_opt(Mel_Render_Default_2D* renderer, Mel_Render_Default_2D_Opt opt)
{
    assert(renderer != nullptr);
    assert(mel_swapchain_handle_valid(opt.swapchain));
    assert(opt.camera != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Gpu_Device* dev = opt.dev ? opt.dev : mel_gpu_dev();
    Mel_Sprite_Pass* sprite_pass = opt.sprite_pass ? opt.sprite_pass : mel_sprite_pass();
    Mel_Text_Pass* text_pass = opt.text_pass ? opt.text_pass : mel_text_pass();

    *renderer = (Mel_Render_Default_2D){
        .swapchain = opt.swapchain,
        .alloc = alloc,
        .dev = dev,
        .sprite_pass = sprite_pass,
        .text_pass = text_pass,
        .imgui_enabled = opt.enable_imgui,
        .install_as_current_graph = opt.install_as_current_graph,
    };
    mel_array_init(&renderer->owned_views, alloc);
    mel_array_init(&renderer->owned_sources, alloc);

    mel_render_graph_init(&renderer->graph,
        .dev = dev && dev->device != VK_NULL_HANDLE ? dev : nullptr,
        .alloc = alloc);

    renderer->view = mel_view_create(&(Mel_View_Desc){
        .name = opt.name.len ? opt.name : S8("default_2d"),
        .camera = opt.camera,
        .clear_color_enabled = opt.clear_color_enabled,
        .clear_color = opt.clear_color,
        .composition_mode = MEL_VIEW_COMPOSE_REPLACE,
    });

    renderer->recipe = mel_frame_recipe_create(opt.name.len ? opt.name : S8("default_2d"));
    renderer->plan = mel_frame_plan_create(opt.name.len ? opt.name : S8("default_2d"));
    mel_array_push(&renderer->owned_views, renderer->view);

    mel_frame_recipe_use_technique(renderer->recipe, renderer->view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(renderer->recipe, renderer->view, renderer->swapchain);
    return true;
}

void mel_render_default_2d_shutdown(Mel_Render_Default_2D* renderer)
{
    assert(renderer != nullptr);

    for (usize i = 0; i < renderer->owned_sources.count; i++)
        mel_source_destroy(renderer->owned_sources.items[i]);
    mel_array_free(&renderer->owned_sources);
    mel__render_default_2d_clear_extra_passes(renderer);

    if (mel_frame_plan_handle_valid(renderer->plan))
        mel_frame_plan_destroy(renderer->plan);
    if (mel_frame_recipe_handle_valid(renderer->recipe))
        mel_frame_recipe_destroy(renderer->recipe);
    for (usize i = 0; i < renderer->owned_views.count; i++)
        if (mel_view_handle_valid(renderer->owned_views.items[i]))
            mel_view_destroy(renderer->owned_views.items[i]);
    mel_array_free(&renderer->owned_views);

    if (renderer->graph.alloc)
        mel_render_graph_shutdown(&renderer->graph);

    *renderer = (Mel_Render_Default_2D){0};
}

bool mel_render_default_2d_attach_sprite_list(Mel_Render_Default_2D* renderer, Mel_Render_List* list)
{
    return mel_render_default_2d_attach_sprite_list_to_view(renderer, renderer->view, list);
}

bool mel_render_default_2d_attach_sprite_list_to_view(Mel_Render_Default_2D* renderer, Mel_View_Handle view, Mel_Render_List* list)
{
    assert(renderer != nullptr);
    assert(mel_view_handle_valid(view));
    assert(list != nullptr);

    Mel_Source_Handle source = MEL_SOURCE_HANDLE_NULL;
    for (usize i = 0; i < renderer->owned_sources.count; i++)
    {
        Mel_Render_List* existing = mel_source_render_list(renderer->owned_sources.items[i]);
        if (existing == list)
        {
            source = renderer->owned_sources.items[i];
            break;
        }
    }

    if (!mel_source_handle_valid(source))
    {
        source = mel_source_from_render_list(list, MEL_SCHEMA_SPRITE);
        mel_array_push(&renderer->owned_sources, source);
    }

    mel_view_attach_source(view, source);
    mel_frame_recipe_use_technique(renderer->recipe, view, MEL_TECHNIQUE_SPRITE);
    return true;
}

bool mel_render_default_2d_attach_text_list(Mel_Render_Default_2D* renderer, Mel_Render_List* list)
{
    return mel_render_default_2d_attach_text_list_to_view(renderer, renderer->view, list);
}

bool mel_render_default_2d_attach_text_list_to_view(Mel_Render_Default_2D* renderer, Mel_View_Handle view, Mel_Render_List* list)
{
    assert(renderer != nullptr);
    assert(mel_view_handle_valid(view));
    assert(list != nullptr);

    Mel_Source_Handle source = MEL_SOURCE_HANDLE_NULL;
    for (usize i = 0; i < renderer->owned_sources.count; i++)
    {
        Mel_Render_List* existing = mel_source_render_list(renderer->owned_sources.items[i]);
        if (existing == list && mel_source_schema(renderer->owned_sources.items[i]) == MEL_SCHEMA_TEXT)
        {
            source = renderer->owned_sources.items[i];
            break;
        }
    }

    if (!mel_source_handle_valid(source))
    {
        source = mel_source_from_render_list(list, MEL_SCHEMA_TEXT);
        mel_array_push(&renderer->owned_sources, source);
    }

    mel_view_attach_source(view, source);
    mel_frame_recipe_use_technique(renderer->recipe, view, MEL_TECHNIQUE_TEXT);
    return true;
}

Mel_View_Handle mel_render_default_2d_add_view_opt(Mel_Render_Default_2D* renderer, Mel_Render_Default_2D_View_Opt opt)
{
    assert(renderer != nullptr);

    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = opt.name.len ? opt.name : S8("default_2d_view"),
        .camera = opt.camera,
        .clear_color_enabled = opt.clear_color_enabled,
        .clear_color = opt.clear_color,
        .composition_mode = opt.composition_mode,
    });

    mel_array_push(&renderer->owned_views, view);
    mel_frame_recipe_use_technique(renderer->recipe, view, MEL_TECHNIQUE_SPRITE);

    if (opt.overlay)
        mel_frame_recipe_overlay_ordered(renderer->recipe, view, renderer->swapchain, opt.order);
    else
        mel_frame_recipe_present_ordered(renderer->recipe, view, renderer->swapchain, opt.order);

    return view;
}

bool mel_render_default_2d_widget_layer_init_opt(Mel_Render_Default_2D* renderer, Mel_Render_Default_2D_Widget_Layer* layer, Mel_Render_Default_2D_Widget_Layer_Opt opt)
{
    assert(renderer != nullptr);
    assert(layer != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : renderer->alloc;
    const Mel_Camera* camera = opt.camera ? opt.camera : mel_view_camera(renderer->view);

    *layer = (Mel_Render_Default_2D_Widget_Layer){
        .root = opt.root,
        .input_scale_x = opt.input_scale_x > 0 ? opt.input_scale_x : 1.0f,
        .input_scale_y = opt.input_scale_y > 0 ? opt.input_scale_y : 1.0f,
        .alloc = alloc,
    };

    mel_render_list_init(&layer->list,
        .name = opt.name.len ? opt.name : S8("widget_layer"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = alloc);
    mel_render_list_add_producer(&layer->list, mel__render_default_2d_widget_producer, layer);

    layer->view = mel_render_default_2d_add_view(renderer,
        .name = opt.name.len ? opt.name : S8("widget_layer"),
        .camera = camera,
        .clear_color_enabled = false,
        .composition_mode = opt.composition_mode ? opt.composition_mode : MEL_VIEW_COMPOSE_ALPHA,
        .overlay = opt.overlay,
        .order = opt.order);

    return mel_render_default_2d_attach_sprite_list_to_view(renderer, layer->view, &layer->list);
}

void mel_render_default_2d_widget_layer_shutdown(Mel_Render_Default_2D* renderer, Mel_Render_Default_2D_Widget_Layer* layer)
{
    MEL_UNUSED(renderer);
    assert(layer != nullptr);
    mel_render_list_shutdown(&layer->list);
    *layer = (Mel_Render_Default_2D_Widget_Layer){0};
}

bool mel_render_default_2d_widget_layer_process_event(Mel_Render_Default_2D_Widget_Layer* layer, const SDL_Event* event)
{
    assert(layer != nullptr);
    assert(event != nullptr);

    if (!layer->root)
        return false;

    switch (event->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            Mel_Vec2 pos = mel_vec2(event->motion.x / layer->input_scale_x,
                event->motion.y / layer->input_scale_y);
            return mel_widget_mouse_move(layer->root, pos);
        } break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            Mel_Vec2 pos = mel_vec2(event->button.x / layer->input_scale_x,
                event->button.y / layer->input_scale_y);
            return mel_widget_mouse_down(layer->root, pos, event->button.button);
        } break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            Mel_Vec2 pos = mel_vec2(event->button.x / layer->input_scale_x,
                event->button.y / layer->input_scale_y);
            return mel_widget_mouse_up(layer->root, pos, event->button.button);
        } break;

        default: return false;
    }
}

bool mel_render_default_2d_rebuild(Mel_Render_Default_2D* renderer)
{
    assert(renderer != nullptr);
    mel__render_default_2d_clear_extra_passes(renderer);

    if (!mel_frame_plan_compile(renderer->plan, renderer->recipe,
        .graph = &renderer->graph,
        .dev = renderer->dev,
        .sprite_pass = renderer->sprite_pass,
        .text_pass = renderer->text_pass))
        return false;

    if (renderer->imgui_enabled)
    {
        Mel_Render_Target* target = mel_frame_plan_swapchain_target(renderer->plan, renderer->swapchain);
        assert(target != nullptr);

        renderer->imgui_pass_name = str8_fmt(renderer->alloc, "default2d.imgui.%u.%u",
            renderer->swapchain.handle.index, renderer->swapchain.handle.generation);

        mel_render_graph_add_pass(&renderer->graph, renderer->imgui_pass_name,
            .fn = mel__render_default_2d_imgui_pass,
            .write_targets = MEL_WRITE_TARGETS(
                { .target = target, .load_op = VK_ATTACHMENT_LOAD_OP_LOAD }));

        if (!mel_render_graph_compile(&renderer->graph))
        {
            mel__render_default_2d_clear_extra_passes(renderer);
            return false;
        }
    }

    if (renderer->install_as_current_graph)
        mel_set_render_graph(&renderer->graph);
    return true;
}

Mel_Render_Graph* mel_render_default_2d_graph(Mel_Render_Default_2D* renderer)
{
    assert(renderer != nullptr);
    return &renderer->graph;
}

Mel_View_Handle mel_render_default_2d_view(Mel_Render_Default_2D* renderer)
{
    assert(renderer != nullptr);
    return renderer->view;
}

Mel_Frame_Recipe_Handle mel_render_default_2d_recipe(Mel_Render_Default_2D* renderer)
{
    assert(renderer != nullptr);
    return renderer->recipe;
}

Mel_Frame_Plan_Handle mel_render_default_2d_plan(Mel_Render_Default_2D* renderer)
{
    assert(renderer != nullptr);
    return renderer->plan;
}

Mel_Render_Target* mel_render_default_2d_target(Mel_Render_Default_2D* renderer)
{
    assert(renderer != nullptr);
    return mel_frame_plan_swapchain_target(renderer->plan, renderer->swapchain);
}

Mel_Render_List* mel_render_default_2d_widget_layer_list(Mel_Render_Default_2D_Widget_Layer* layer)
{
    assert(layer != nullptr);
    return &layer->list;
}
