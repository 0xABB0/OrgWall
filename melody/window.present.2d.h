#pragma once

#include "window.fwd.h"
#include "ecs.world.fwd.h"
#include "render.stage.2d.h"
#include "render.list.fwd.h"
#include "font.atlas.fwd.h"
#include "allocator.fwd.h"

#include <SDL3/SDL_events.h>

typedef struct { Mel_SlotMap_Handle handle; } Mel_Window_Present_2D_Handle;
#define MEL_WINDOW_PRESENT_2D_HANDLE_NULL ((Mel_Window_Present_2D_Handle){0})

static inline bool mel_window_present_2d_handle_valid(Mel_Window_Present_2D_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}

typedef struct {
    str8 name;
    Mel_ECS* world;
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
    const Mel_Alloc* alloc;
} Mel_Window_Present_2D_Opt;

Mel_Window_Present_2D_Handle mel_window_present_world_2d_opt(Mel_Window_Handle window, Mel_Window_Present_2D_Opt opt);
#define mel_window_present_world_2d(window, ...) \
    mel_window_present_world_2d_opt((window), (Mel_Window_Present_2D_Opt){__VA_ARGS__})

void mel_window_unpresent_2d(Mel_Window_Present_2D_Handle handle);

bool mel_window_present_2d_attach_sprite_list(Mel_Window_Present_2D_Handle handle, Mel_Render_List* list);
bool mel_window_present_2d_attach_sprite_list_to_layer(Mel_Window_Present_2D_Handle handle, Mel_Render_Stage_2D_Layer layer, Mel_Render_List* list);
bool mel_window_present_2d_attach_text_list(Mel_Window_Present_2D_Handle handle, Mel_Render_List* list);
bool mel_window_present_2d_attach_text_list_to_layer(Mel_Window_Present_2D_Handle handle, Mel_Render_Stage_2D_Layer layer, Mel_Render_List* list);

Mel_Render_Stage_2D* mel_window_present_2d_stage(Mel_Window_Present_2D_Handle handle);

void mel__window_present_2d_tick(void);
void mel__window_present_2d_process_event(const SDL_Event* event);
void mel__window_present_2d_shutdown_all(void);
