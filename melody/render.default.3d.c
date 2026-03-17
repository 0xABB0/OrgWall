#include "render.default.3d.h"
#include "render.view.h"
#include "render.frame_recipe.h"
#include "render.frame_plan.h"
#include "render.source.h"
#include "mesh.pass.h"
#include "sprite.pass.h"
#include "text.pass.h"
#include "core.engine.h"
#include "gpu.device.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "ui.widget.h"
#include "math.vec2.h"

static void mel__render_default_3d_widget_producer(Mel_Render_List* list, void* user)
{
    Mel_Render_Default_3D_Widget_Layer* layer = user;
    if (layer->root)
        mel_widget_draw(layer->root, list);
}

bool mel_render_default_3d_init_opt(Mel_Render_Default_3D* renderer, Mel_Render_Default_3D_Opt opt)
{
    assert(renderer != nullptr);
    assert(mel_swapchain_handle_valid(opt.swapchain));
    assert(opt.camera != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Gpu_Device* dev = opt.dev ? opt.dev : mel_gpu_dev();
    *renderer = (Mel_Render_Default_3D){
        .swapchain = opt.swapchain,
        .alloc = alloc,
        .dev = dev,
        .mesh_pass = opt.mesh_pass,
        .sprite_pass = opt.sprite_pass,
        .text_pass = opt.text_pass,
        .imgui_enabled = opt.enable_imgui,
        .imgui_fn = opt.imgui_fn,
        .imgui_user = opt.imgui_user,
        .install_as_current_graph = opt.install_as_current_graph,
    };
    renderer->imgui_callback = (Mel_ImGui_Draw_Callback){
        .fn = opt.imgui_fn,
        .user = opt.imgui_user,
    };
    mel_array_init(&renderer->owned_views, alloc);
    mel_array_init(&renderer->owned_sources, alloc);

    mel_render_graph_init(&renderer->graph,
        .dev = dev && dev->ready ? dev : nullptr,
        .alloc = alloc);

    renderer->view = mel_view_create(&(Mel_View_Desc){
        .name = opt.name.len ? opt.name : S8("default_3d"),
        .camera = opt.camera,
        .clear_color_enabled = opt.clear_color_enabled,
        .clear_color = opt.clear_color,
        .composition_mode = MEL_VIEW_COMPOSE_REPLACE,
    });
    mel_array_push(&renderer->owned_views, renderer->view);

    renderer->recipe = mel_frame_recipe_create(opt.name.len ? opt.name : S8("default_3d"));
    renderer->plan = mel_frame_plan_create(opt.name.len ? opt.name : S8("default_3d"));
    mel_frame_recipe_use_technique(renderer->recipe, renderer->view, MEL_TECHNIQUE_MESH);
    mel_frame_recipe_present(renderer->recipe, renderer->view, renderer->swapchain);

    if (renderer->imgui_enabled)
    {
        renderer->imgui_view = mel_view_create(&(Mel_View_Desc){
            .name = S8("default_3d_imgui"),
            .camera = opt.camera,
            .clear_color_enabled = false,
            .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
            .user = &renderer->imgui_callback,
        });
        mel_array_push(&renderer->owned_views, renderer->imgui_view);
        mel_frame_recipe_use_technique(renderer->recipe, renderer->imgui_view, MEL_TECHNIQUE_IMGUI);
        mel_frame_recipe_overlay_ordered(renderer->recipe, renderer->imgui_view, renderer->swapchain, 1000000);
    }

    return true;
}

void mel_render_default_3d_shutdown(Mel_Render_Default_3D* renderer)
{
    assert(renderer != nullptr);

    for (usize i = 0; i < renderer->owned_sources.count; i++)
        mel_source_destroy(renderer->owned_sources.items[i]);
    mel_array_free(&renderer->owned_sources);

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

    *renderer = (Mel_Render_Default_3D){0};
}

bool mel_render_default_3d_attach_mesh_list(Mel_Render_Default_3D* renderer, Mel_Render_List* list)
{
    return mel_render_default_3d_attach_mesh_list_to_view(renderer, renderer->view, list);
}

bool mel_render_default_3d_attach_mesh_source_to_view(Mel_Render_Default_3D* renderer, Mel_View_Handle view, Mel_Source_Handle source)
{
    assert(renderer != nullptr);
    assert(mel_view_handle_valid(view));
    assert(mel_source_handle_valid(source));
    assert(mel_source_schema(source) == MEL_SCHEMA_MESH_INSTANCE ||
        mel_source_schema(source) == MEL_SCHEMA_MESH_DRAW_STREAM ||
        mel_source_schema(source) == MEL_SCHEMA_MESH_INDIRECT_STREAM ||
        mel_source_schema(source) == MEL_SCHEMA_MESH_CULL_STREAM ||
        mel_source_schema(source) == MEL_SCHEMA_MESH_CULL_BATCH_STREAM ||
        mel_source_schema(source) == MEL_SCHEMA_MATERIAL_TABLE ||
        mel_source_schema(source) == MEL_SCHEMA_LIGHT);

    mel_view_attach_source(view, source);
    mel_frame_recipe_use_technique(renderer->recipe, view, MEL_TECHNIQUE_MESH);
    return true;
}

bool mel_render_default_3d_attach_light_source_to_view(Mel_Render_Default_3D* renderer, Mel_View_Handle view, Mel_Source_Handle source)
{
    assert(renderer != nullptr);
    assert(mel_view_handle_valid(view));
    assert(mel_source_handle_valid(source));
    assert(mel_source_schema(source) == MEL_SCHEMA_LIGHT);

    mel_view_attach_source(view, source);
    mel_frame_recipe_use_technique(renderer->recipe, view, MEL_TECHNIQUE_MESH);
    return true;
}

bool mel_render_default_3d_attach_mesh_list_to_view(Mel_Render_Default_3D* renderer, Mel_View_Handle view, Mel_Render_List* list)
{
    assert(renderer != nullptr);
    assert(mel_view_handle_valid(view));
    assert(list != nullptr);

    Mel_Source_Handle source = MEL_SOURCE_HANDLE_NULL;
    for (usize i = 0; i < renderer->owned_sources.count; i++)
    {
        Mel_Render_List* existing = mel_source_render_list(renderer->owned_sources.items[i]);
        if (existing == list && mel_source_schema(renderer->owned_sources.items[i]) == MEL_SCHEMA_MESH_INSTANCE)
        {
            source = renderer->owned_sources.items[i];
            break;
        }
    }

    if (!mel_source_handle_valid(source))
    {
        source = mel_source_from_render_list(list, MEL_SCHEMA_MESH_INSTANCE);
        mel_array_push(&renderer->owned_sources, source);
    }

    return mel_render_default_3d_attach_mesh_source_to_view(renderer, view, source);
}

bool mel_render_default_3d_attach_sprite_list_to_view_family(Mel_Render_Default_3D* renderer, Mel_View_Handle view, Mel_Render_List* list, Mel_Technique_Family_Id family)
{
    assert(renderer != nullptr);
    assert(mel_view_handle_valid(view));
    assert(list != nullptr);
    assert(family != MEL_TECHNIQUE_NONE);

    Mel_Source_Handle source = MEL_SOURCE_HANDLE_NULL;
    for (usize i = 0; i < renderer->owned_sources.count; i++)
    {
        Mel_Render_List* existing = mel_source_render_list(renderer->owned_sources.items[i]);
        if (existing == list && mel_source_schema(renderer->owned_sources.items[i]) == MEL_SCHEMA_SPRITE)
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
    mel_frame_recipe_use_technique(renderer->recipe, view, family);
    return true;
}

bool mel_render_default_3d_attach_text_list_to_view(Mel_Render_Default_3D* renderer, Mel_View_Handle view, Mel_Render_List* list)
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

Mel_View_Handle mel_render_default_3d_add_view_opt(Mel_Render_Default_3D* renderer, Mel_Render_Default_3D_View_Opt opt)
{
    assert(renderer != nullptr);

    Mel_View_Handle view = mel_view_create(&(Mel_View_Desc){
        .name = opt.name.len ? opt.name : S8("default_3d_view"),
        .camera = opt.camera,
        .clear_color_enabled = opt.clear_color_enabled,
        .clear_color = opt.clear_color,
        .composition_mode = opt.composition_mode,
    });

    mel_array_push(&renderer->owned_views, view);
    mel_frame_recipe_use_technique(renderer->recipe, view,
        opt.technique_family != MEL_TECHNIQUE_NONE ? opt.technique_family : MEL_TECHNIQUE_MESH);

    if (opt.overlay)
        mel_frame_recipe_overlay_ordered(renderer->recipe, view, renderer->swapchain, opt.order);
    else
        mel_frame_recipe_present_ordered(renderer->recipe, view, renderer->swapchain, opt.order);

    return view;
}

bool mel_render_default_3d_widget_layer_init_opt(Mel_Render_Default_3D* renderer, Mel_Render_Default_3D_Widget_Layer* layer, Mel_Render_Default_3D_Widget_Layer_Opt opt)
{
    assert(renderer != nullptr);
    assert(layer != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : renderer->alloc;
    const Mel_Camera* camera = opt.camera ? opt.camera : mel_view_camera(renderer->view);

    *layer = (Mel_Render_Default_3D_Widget_Layer){
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
    mel_render_list_add_producer(&layer->list, mel__render_default_3d_widget_producer, layer);

    layer->view = mel_render_default_3d_add_view(renderer,
        .name = opt.name.len ? opt.name : S8("widget_layer"),
        .camera = camera,
        .clear_color_enabled = false,
        .composition_mode = opt.composition_mode ? opt.composition_mode : MEL_VIEW_COMPOSE_ALPHA,
        .technique_family = MEL_TECHNIQUE_UI,
        .overlay = opt.overlay,
        .order = opt.order);

    return mel_render_default_3d_attach_sprite_list_to_view_family(renderer, layer->view, &layer->list, MEL_TECHNIQUE_UI);
}

void mel_render_default_3d_widget_layer_shutdown(Mel_Render_Default_3D* renderer, Mel_Render_Default_3D_Widget_Layer* layer)
{
    MEL_UNUSED(renderer);
    assert(layer != nullptr);
    mel_render_list_shutdown(&layer->list);
    *layer = (Mel_Render_Default_3D_Widget_Layer){0};
}

bool mel_render_default_3d_widget_layer_process_event(Mel_Render_Default_3D_Widget_Layer* layer, const SDL_Event* event)
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

bool mel_render_default_3d_rebuild(Mel_Render_Default_3D* renderer)
{
    assert(renderer != nullptr);

    if (!mel_frame_plan_compile(renderer->plan, renderer->recipe,
        .graph = &renderer->graph,
        .dev = renderer->dev,
        .mesh_pass = renderer->mesh_pass,
        .sprite_pass = renderer->sprite_pass,
        .text_pass = renderer->text_pass))
        return false;

    if (renderer->install_as_current_graph)
        mel_set_render_graph(&renderer->graph);
    return true;
}

bool mel_render_default_3d_refresh(Mel_Render_Default_3D* renderer)
{
    assert(renderer != nullptr);

    if (!mel_frame_plan_refresh(renderer->plan, .dev = renderer->dev))
        return false;

    if (renderer->install_as_current_graph)
        mel_set_render_graph(&renderer->graph);
    return true;
}

Mel_Render_Graph* mel_render_default_3d_graph(Mel_Render_Default_3D* renderer)
{
    assert(renderer != nullptr);
    return &renderer->graph;
}

Mel_View_Handle mel_render_default_3d_view(Mel_Render_Default_3D* renderer)
{
    assert(renderer != nullptr);
    return renderer->view;
}

Mel_Frame_Recipe_Handle mel_render_default_3d_recipe(Mel_Render_Default_3D* renderer)
{
    assert(renderer != nullptr);
    return renderer->recipe;
}

Mel_Frame_Plan_Handle mel_render_default_3d_plan(Mel_Render_Default_3D* renderer)
{
    assert(renderer != nullptr);
    return renderer->plan;
}

Mel_Render_Target* mel_render_default_3d_target(Mel_Render_Default_3D* renderer)
{
    assert(renderer != nullptr);
    return mel_frame_plan_swapchain_target(renderer->plan, renderer->swapchain);
}

Mel_Render_List* mel_render_default_3d_widget_layer_list(Mel_Render_Default_3D_Widget_Layer* layer)
{
    assert(layer != nullptr);
    return &layer->list;
}
