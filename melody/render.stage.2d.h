#pragma once

#include "core.types.h"
#include "render.default.2d.h"

typedef enum {
    MEL_RENDER_STAGE_2D_LAYER_WORLD = 0,
    MEL_RENDER_STAGE_2D_LAYER_HUD,
    MEL_RENDER_STAGE_2D_LAYER_DEBUG,
    MEL_RENDER_STAGE_2D_LAYER_UI,
    MEL_RENDER_STAGE_2D_LAYER_COUNT,
} Mel_Render_Stage_2D_Layer;

typedef struct {
    str8 name;
    Mel_Swapchain_Handle swapchain;
    const Mel_Camera* world_camera;
    const Mel_Camera* hud_camera;
    const Mel_Camera* debug_camera;
    const Mel_Camera* ui_camera;
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
} Mel_Render_Stage_2D_Opt;

typedef struct {
    str8 name;
    Mel_Widget* root;
    Mel_Render_Stage_2D_Layer layer;
    f32 input_scale_x;
    f32 input_scale_y;
    const Mel_Alloc* alloc;
} Mel_Render_Stage_2D_Widget_Layer_Opt;

typedef struct {
    Mel_Render_Default_2D renderer;
    Mel_View_Handle views[MEL_RENDER_STAGE_2D_LAYER_COUNT];
} Mel_Render_Stage_2D;

typedef struct {
    Mel_Widget* root;
    Mel_Render_Stage_2D_Layer layer;
    Mel_Render_List list;
    Mel_Widget* focused;
    f32 input_scale_x;
    f32 input_scale_y;
    const Mel_Alloc* alloc;
} Mel_Render_Stage_2D_Widget_Layer;

bool mel_render_stage_2d_init_opt(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Opt opt);
#define mel_render_stage_2d_init(stage, ...) mel_render_stage_2d_init_opt((stage), (Mel_Render_Stage_2D_Opt){__VA_ARGS__})

void mel_render_stage_2d_shutdown(Mel_Render_Stage_2D* stage);

bool mel_render_stage_2d_attach_sprite_list(Mel_Render_Stage_2D* stage, Mel_Render_List* list);
bool mel_render_stage_2d_attach_sprite_list_to_layer(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Layer layer, Mel_Render_List* list);
bool mel_render_stage_2d_attach_text_list(Mel_Render_Stage_2D* stage, Mel_Render_List* list);
bool mel_render_stage_2d_attach_text_list_to_layer(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Layer layer, Mel_Render_List* list);

bool mel_render_stage_2d_widget_layer_init_opt(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Widget_Layer* layer, Mel_Render_Stage_2D_Widget_Layer_Opt opt);
#define mel_render_stage_2d_widget_layer_init(stage, layer, ...) mel_render_stage_2d_widget_layer_init_opt((stage), (layer), (Mel_Render_Stage_2D_Widget_Layer_Opt){__VA_ARGS__})
void mel_render_stage_2d_widget_layer_shutdown(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Widget_Layer* layer);
bool mel_render_stage_2d_widget_layer_process_event(Mel_Render_Stage_2D_Widget_Layer* layer, const SDL_Event* event);

bool mel_render_stage_2d_rebuild(Mel_Render_Stage_2D* stage);
bool mel_render_stage_2d_refresh(Mel_Render_Stage_2D* stage);

Mel_View_Handle mel_render_stage_2d_view(Mel_Render_Stage_2D* stage, Mel_Render_Stage_2D_Layer layer);
Mel_Render_Default_2D* mel_render_stage_2d_renderer(Mel_Render_Stage_2D* stage);
Mel_Render_Graph* mel_render_stage_2d_graph(Mel_Render_Stage_2D* stage);
Mel_Frame_Recipe_Handle mel_render_stage_2d_recipe(Mel_Render_Stage_2D* stage);
Mel_Frame_Plan_Handle mel_render_stage_2d_plan(Mel_Render_Stage_2D* stage);
Mel_Render_Target* mel_render_stage_2d_target(Mel_Render_Stage_2D* stage);
Mel_Render_List* mel_render_stage_2d_widget_layer_list(Mel_Render_Stage_2D_Widget_Layer* layer);
