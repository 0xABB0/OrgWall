#pragma once

#include "core.types.h"
#include "collection.array.h"
#include "render.graph.h"
#include "render.view.fwd.h"
#include "render.frame_recipe.fwd.h"
#include "render.frame_plan.fwd.h"
#include "render.camera.fwd.h"
#include "render.list.h"
#include "render.source.fwd.h"
#include "render.technique.h"
#include "swapchain.fwd.h"
#include "gpu.device.fwd.h"
#include "sprite.pass.fwd.h"
#include "text.pass.fwd.h"
#include "ui.widget.fwd.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"
#include "math.vec4.h"
#include "render.target.fwd.h"
#include <SDL3/SDL_events.h>

typedef void (*Mel_Render_Default_2D_ImGui_Fn)(void* user);

typedef struct {
    str8 name;
    Mel_Swapchain_Handle swapchain;
    const Mel_Camera* camera;
    bool clear_color_enabled;
    Mel_Vec4 clear_color;
    u32 design_width;
    u32 design_height;
    bool enable_imgui;
    Mel_Render_Default_2D_ImGui_Fn imgui_fn;
    void* imgui_user;
    bool install_as_current_graph;
    Mel_Gpu_Device* dev;
    Mel_Sprite_Pass* sprite_pass;
    Mel_Text_Pass* text_pass;
    const Mel_Alloc* alloc;
} Mel_Render_Default_2D_Opt;

typedef struct {
    str8 name;
    const Mel_Camera* camera;
    bool clear_color_enabled;
    Mel_Vec4 clear_color;
    u32 composition_mode;
    Mel_Technique_Family_Id technique_family;
    u32 design_width;
    u32 design_height;
    bool overlay;
    i32 order;
} Mel_Render_Default_2D_View_Opt;

typedef struct {
    str8 name;
    Mel_Widget* root;
    const Mel_Camera* camera;
    bool overlay;
    i32 order;
    f32 input_scale_x;
    f32 input_scale_y;
    u32 composition_mode;
    const Mel_Alloc* alloc;
} Mel_Render_Default_2D_Widget_Layer_Opt;

typedef struct {
    Mel_Swapchain_Handle swapchain;
    Mel_Render_Graph graph;
    Mel_View_Handle view;
    Mel_Frame_Recipe_Handle recipe;
    Mel_Frame_Plan_Handle plan;
    Mel_Array(Mel_View_Handle) owned_views;
    Mel_Array(Mel_Source_Handle) owned_sources;
    Mel_View_Handle imgui_view;
    Mel_ImGui_Draw_Callback imgui_callback;
    const Mel_Alloc* alloc;
    Mel_Gpu_Device* dev;
    Mel_Sprite_Pass* sprite_pass;
    Mel_Text_Pass* text_pass;
    bool imgui_enabled;
    Mel_Render_Default_2D_ImGui_Fn imgui_fn;
    void* imgui_user;
    bool install_as_current_graph;
} Mel_Render_Default_2D;

typedef struct {
    Mel_Widget* root;
    Mel_View_Handle view;
    Mel_Render_List list;
    Mel_Widget* focused;
    f32 input_scale_x;
    f32 input_scale_y;
    const Mel_Alloc* alloc;
} Mel_Render_Default_2D_Widget_Layer;

bool mel_render_default_2d_init_opt(Mel_Render_Default_2D* renderer, Mel_Render_Default_2D_Opt opt);
#define mel_render_default_2d_init(renderer, ...) mel_render_default_2d_init_opt((renderer), (Mel_Render_Default_2D_Opt){__VA_ARGS__})

void mel_render_default_2d_shutdown(Mel_Render_Default_2D* renderer);

bool mel_render_default_2d_attach_sprite_list(Mel_Render_Default_2D* renderer, Mel_Render_List* list);
bool mel_render_default_2d_attach_sprite_list_to_view(Mel_Render_Default_2D* renderer, Mel_View_Handle view, Mel_Render_List* list);
bool mel_render_default_2d_attach_sprite_list_to_view_family(Mel_Render_Default_2D* renderer, Mel_View_Handle view, Mel_Render_List* list, Mel_Technique_Family_Id family);
bool mel_render_default_2d_attach_text_list(Mel_Render_Default_2D* renderer, Mel_Render_List* list);
bool mel_render_default_2d_attach_text_list_to_view(Mel_Render_Default_2D* renderer, Mel_View_Handle view, Mel_Render_List* list);
Mel_View_Handle mel_render_default_2d_add_view_opt(Mel_Render_Default_2D* renderer, Mel_Render_Default_2D_View_Opt opt);
#define mel_render_default_2d_add_view(renderer, ...) mel_render_default_2d_add_view_opt((renderer), (Mel_Render_Default_2D_View_Opt){__VA_ARGS__})
bool mel_render_default_2d_widget_layer_init_opt(Mel_Render_Default_2D* renderer, Mel_Render_Default_2D_Widget_Layer* layer, Mel_Render_Default_2D_Widget_Layer_Opt opt);
#define mel_render_default_2d_widget_layer_init(renderer, layer, ...) mel_render_default_2d_widget_layer_init_opt((renderer), (layer), (Mel_Render_Default_2D_Widget_Layer_Opt){__VA_ARGS__})
void mel_render_default_2d_widget_layer_shutdown(Mel_Render_Default_2D* renderer, Mel_Render_Default_2D_Widget_Layer* layer);
bool mel_render_default_2d_widget_layer_process_event(Mel_Render_Default_2D_Widget_Layer* layer, const SDL_Event* event);
bool mel_render_default_2d_rebuild(Mel_Render_Default_2D* renderer);
bool mel_render_default_2d_refresh(Mel_Render_Default_2D* renderer);

Mel_Render_Graph* mel_render_default_2d_graph(Mel_Render_Default_2D* renderer);
Mel_View_Handle mel_render_default_2d_view(Mel_Render_Default_2D* renderer);
Mel_Frame_Recipe_Handle mel_render_default_2d_recipe(Mel_Render_Default_2D* renderer);
Mel_Frame_Plan_Handle mel_render_default_2d_plan(Mel_Render_Default_2D* renderer);
Mel_Render_Target* mel_render_default_2d_target(Mel_Render_Default_2D* renderer);
Mel_Render_List* mel_render_default_2d_widget_layer_list(Mel_Render_Default_2D_Widget_Layer* layer);
