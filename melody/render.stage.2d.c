#include "render.stage.2d.h"
#include "render.view.h"
#include "sprite.pass.h"
#include "string.str8.h"
#include "ui.widget.h"
#include "math.vec2.h"

static void mel__render_stage_2d_widget_producer(Mel_Render_List* list, void* user)
{
    Mel_Render_Stage_2D_Widget_Layer* layer = user;
    if (layer->root)
        mel_widget_draw(layer->root, list);
}

static void mel__render_stage_2d_widget_focus(Mel_Render_Stage_2D_Widget_Layer* layer, Mel_Widget* widget)
{
    if (layer->focused == widget)
        return;

    if (layer->focused)
        layer->focused->state &= ~MEL_WIDGET_STATE_FOCUSED;
    layer->focused = widget;
    if (layer->focused)
        layer->focused->state |= MEL_WIDGET_STATE_FOCUSED;
}

static Mel_View_Handle mel__render_stage_2d_require_view(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Layer layer)
{
    assert(stage != nullptr);
    assert(layer < MEL_RENDER_STAGE_2D_LAYER_COUNT);
    Mel_View_Handle view = stage->views[layer];
    assert(mel_view_handle_valid(view));
    return view;
}

bool mel_render_stage_2d_init_opt(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Opt opt)
{
    assert(stage != nullptr);
    assert(opt.world_camera != nullptr);

    bool ok = mel_render_default_2d_init(&stage->renderer,
        .name = opt.name.len ? opt.name : S8("stage_2d"),
        .swapchain = opt.swapchain,
        .camera = opt.world_camera,
        .clear_color_enabled = opt.clear_color_enabled,
        .clear_color = opt.clear_color,
        .design_width = opt.design_width,
        .design_height = opt.design_height,
        .enable_imgui = opt.enable_imgui,
        .imgui_fn = opt.imgui_fn,
        .imgui_user = opt.imgui_user,
        .install_as_current_graph = opt.install_as_current_graph,
        .dev = opt.dev,
        .sprite_pass = opt.sprite_pass,
        .text_pass = opt.text_pass,
        .alloc = opt.alloc);
    if (!ok)
        return false;

    stage->views[MEL_RENDER_STAGE_2D_LAYER_WORLD] = mel_render_default_2d_view(&stage->renderer);
    stage->views[MEL_RENDER_STAGE_2D_LAYER_HUD] = mel_render_default_2d_add_view(&stage->renderer,
        .name = S8("hud"),
        .camera = opt.hud_camera ? opt.hud_camera : opt.world_camera,
        .clear_color_enabled = false,
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
        .design_width = opt.design_width,
        .design_height = opt.design_height,
        .overlay = true,
        .order = 100);
    stage->views[MEL_RENDER_STAGE_2D_LAYER_DEBUG] = mel_render_default_2d_add_view(&stage->renderer,
        .name = S8("debug"),
        .camera = opt.debug_camera ? opt.debug_camera : opt.world_camera,
        .clear_color_enabled = false,
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
        .technique_family = MEL_TECHNIQUE_DEBUG,
        .design_width = opt.design_width,
        .design_height = opt.design_height,
        .overlay = true,
        .order = 200);
    stage->views[MEL_RENDER_STAGE_2D_LAYER_UI] = mel_render_default_2d_add_view(&stage->renderer,
        .name = S8("ui"),
        .camera = opt.ui_camera ? opt.ui_camera : (opt.hud_camera ? opt.hud_camera : opt.world_camera),
        .clear_color_enabled = false,
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
        .technique_family = MEL_TECHNIQUE_UI,
        .design_width = opt.design_width,
        .design_height = opt.design_height,
        .overlay = true,
        .order = 300);
    return true;
}

void mel_render_stage_2d_shutdown(Mel_Render_Stage_2D* stage)
{
    assert(stage != nullptr);
    mel_render_default_2d_shutdown(&stage->renderer);
    *stage = (Mel_Render_Stage_2D){0};
}

bool mel_render_stage_2d_attach_sprite_list(Mel_Render_Stage_2D* stage, Mel_Render_List* list)
{
    return mel_render_stage_2d_attach_sprite_list_to_layer(stage, MEL_RENDER_STAGE_2D_LAYER_WORLD, list);
}

bool mel_render_stage_2d_attach_sprite_list_to_layer(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Layer layer, Mel_Render_List* list)
{
    Mel_View_Handle view = mel__render_stage_2d_require_view(stage, layer);
    Mel_Technique_Family_Id family = layer == MEL_RENDER_STAGE_2D_LAYER_DEBUG
        ? MEL_TECHNIQUE_DEBUG
        : (layer == MEL_RENDER_STAGE_2D_LAYER_UI ? MEL_TECHNIQUE_UI : MEL_TECHNIQUE_SPRITE);
    return mel_render_default_2d_attach_sprite_list_to_view_family(&stage->renderer, view, list, family);
}

bool mel_render_stage_2d_attach_text_list(Mel_Render_Stage_2D* stage, Mel_Render_List* list)
{
    return mel_render_stage_2d_attach_text_list_to_layer(stage, MEL_RENDER_STAGE_2D_LAYER_WORLD, list);
}

bool mel_render_stage_2d_attach_text_list_to_layer(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Layer layer, Mel_Render_List* list)
{
    Mel_View_Handle view = mel__render_stage_2d_require_view(stage, layer);
    return mel_render_default_2d_attach_text_list_to_view(&stage->renderer, view, list);
}

bool mel_render_stage_2d_widget_layer_init_opt(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Widget_Layer* layer, Mel_Render_Stage_2D_Widget_Layer_Opt opt)
{
    assert(stage != nullptr);
    assert(layer != nullptr);

    Mel_Render_Stage_2D_Layer target_layer = opt.layer < MEL_RENDER_STAGE_2D_LAYER_COUNT ? opt.layer : MEL_RENDER_STAGE_2D_LAYER_UI;
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : stage->renderer.alloc;

    *layer = (Mel_Render_Stage_2D_Widget_Layer){
        .root = opt.root,
        .layer = target_layer,
        .input_scale_x = opt.input_scale_x > 0 ? opt.input_scale_x : 1.0f,
        .input_scale_y = opt.input_scale_y > 0 ? opt.input_scale_y : 1.0f,
        .alloc = alloc,
    };

    mel_render_list_init(&layer->list,
        .name = opt.name.len ? opt.name : S8("stage_2d_widget_layer"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = alloc);
    mel_render_list_add_producer(&layer->list, mel__render_stage_2d_widget_producer, layer);

    if (!mel_render_stage_2d_attach_sprite_list_to_layer(stage, target_layer, &layer->list))
    {
        mel_render_list_shutdown(&layer->list);
        *layer = (Mel_Render_Stage_2D_Widget_Layer){0};
        return false;
    }

    return true;
}

void mel_render_stage_2d_widget_layer_shutdown(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Widget_Layer* layer)
{
    MEL_UNUSED(stage);
    assert(layer != nullptr);
    mel_render_list_shutdown(&layer->list);
    *layer = (Mel_Render_Stage_2D_Widget_Layer){0};
}

bool mel_render_stage_2d_widget_layer_process_event(Mel_Render_Stage_2D_Widget_Layer* layer, const SDL_Event* event)
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
            mel__render_stage_2d_widget_focus(layer, mel_widget_hit_test(layer->root, pos));
            return mel_widget_mouse_down(layer->root, pos, event->button.button);
        } break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            Mel_Vec2 pos = mel_vec2(event->button.x / layer->input_scale_x,
                event->button.y / layer->input_scale_y);
            return mel_widget_mouse_up(layer->root, pos, event->button.button);
        } break;

        case SDL_EVENT_KEY_DOWN:
            return layer->focused ? mel_widget_key_down(layer->focused, &event->key) : false;

        default: return false;
    }
}

bool mel_render_stage_2d_rebuild(Mel_Render_Stage_2D* stage)
{
    assert(stage != nullptr);
    return mel_render_default_2d_rebuild(&stage->renderer);
}

bool mel_render_stage_2d_refresh(Mel_Render_Stage_2D* stage)
{
    assert(stage != nullptr);
    return mel_render_default_2d_refresh(&stage->renderer);
}

Mel_View_Handle mel_render_stage_2d_view(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Layer layer)
{
    return mel__render_stage_2d_require_view(stage, layer);
}

Mel_Render_Default_2D* mel_render_stage_2d_renderer(Mel_Render_Stage_2D* stage)
{
    assert(stage != nullptr);
    return &stage->renderer;
}

Mel_Render_Graph* mel_render_stage_2d_graph(Mel_Render_Stage_2D* stage)
{
    assert(stage != nullptr);
    return mel_render_default_2d_graph(&stage->renderer);
}

Mel_Frame_Recipe_Handle mel_render_stage_2d_recipe(Mel_Render_Stage_2D* stage)
{
    assert(stage != nullptr);
    return mel_render_default_2d_recipe(&stage->renderer);
}

Mel_Frame_Plan_Handle mel_render_stage_2d_plan(Mel_Render_Stage_2D* stage)
{
    assert(stage != nullptr);
    return mel_render_default_2d_plan(&stage->renderer);
}

Mel_Render_Target* mel_render_stage_2d_target(Mel_Render_Stage_2D* stage)
{
    assert(stage != nullptr);
    return mel_render_default_2d_target(&stage->renderer);
}

Mel_Render_List* mel_render_stage_2d_widget_layer_list(Mel_Render_Stage_2D_Widget_Layer* layer)
{
    assert(layer != nullptr);
    return &layer->list;
}
