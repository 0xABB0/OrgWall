#pragma once

#include "core.types.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"
#include "gpu.device.fwd.h"
#include "swapchain.fwd.h"
#include "render.graph.fwd.h"
#include "sprite.pass.fwd.h"
#include "texture.pool.fwd.h"
#include "sim.ctx.fwd.h"
#include "window.fwd.h"

#include <SDL3/SDL.h>

typedef struct {
    str8 app_name;
    const Mel_Alloc* allocator;
    bool enable_validation;
    f32 max_frame_time;
} Mel_Init_Opt;

bool mel_init_opt(Mel_Init_Opt opt);
#define mel_init(...) mel_init_opt((Mel_Init_Opt){__VA_ARGS__})

void mel_shutdown(void);

Mel_Gpu_Device*    mel_gpu_dev(void);
Mel_Sprite_Pass*   mel_sprite_pass(void);
Mel_Texture_Pool*  mel_texture_pool(void);
const Mel_Alloc*   mel_allocator(void);

void mel_set_render_graph(Mel_Render_Graph* graph);

void mel_register_sim(Mel_Sim_Ctx* sim);
void mel_unregister_sim(Mel_Sim_Ctx* sim);

void mel_frame(void);
void mel_process_event(SDL_Event* event);

bool mel_imgui_init(Mel_Window_Handle window, Mel_Swapchain* swapchain);

void mel__engine_init(void);
void mel__engine_shutdown(void);
