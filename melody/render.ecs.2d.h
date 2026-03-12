#pragma once

#include "core.types.h"
#include "render.stage.2d.h"
#include "render.sync.h"
#include "font.atlas.fwd.h"
#include "allocator.fwd.h"

#include <flecs.h>

typedef struct {
    str8 name;
    ecs_world_t* world;
    Mel_Swapchain_Handle swapchain;
    const Mel_Camera* world_camera;
    const Mel_Camera* hud_camera;
    const Mel_Camera* debug_camera;
    const Mel_Camera* ui_camera;
    Mel_Render_Stage_2D_Layer sprite_layer;
    Mel_Render_Stage_2D_Layer text_layer;
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
    Mel_Font_Atlas_Pool* font_pool;
    const Mel_Alloc* alloc;
} Mel_Render_ECS_2D_Opt;

typedef struct {
    Mel_Render_Stage_2D stage;
    Mel_Render_List sprite_list;
    Mel_Render_List text_list;
    Mel_Render_Sync sprite_sync;
    ecs_world_t* world;
    Mel_Font_Atlas_Pool* font_pool;
    const Mel_Alloc* alloc;
} Mel_Render_ECS_2D;

bool mel_render_ecs_2d_init_opt(Mel_Render_ECS_2D* renderer, Mel_Render_ECS_2D_Opt opt);
#define mel_render_ecs_2d_init(renderer, ...) mel_render_ecs_2d_init_opt((renderer), (Mel_Render_ECS_2D_Opt){__VA_ARGS__})

void mel_render_ecs_2d_shutdown(Mel_Render_ECS_2D* renderer);
void mel_render_ecs_2d_extract(Mel_Render_ECS_2D* renderer);

Mel_Render_Stage_2D* mel_render_ecs_2d_stage(Mel_Render_ECS_2D* renderer);
Mel_Render_List* mel_render_ecs_2d_sprite_list(Mel_Render_ECS_2D* renderer);
Mel_Render_List* mel_render_ecs_2d_text_list(Mel_Render_ECS_2D* renderer);
