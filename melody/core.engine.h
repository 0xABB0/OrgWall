#pragma once

#include "core.types.h"
#include "sim.ctx.fwd.h"
#include "render.graph.fwd.h"
#include "window.fwd.h"
#include "swapchain.fwd.h"

#include <SDL3/SDL.h>

typedef struct {
    f32 dt;
    f32 fps;
    u64 frame_count;
} Mel_Frame_Stats;

void mel_boot(void);
void mel_shutdown(void);

void mel_set_render_graph(Mel_Render_Graph* graph);

void mel_register_sim(Mel_Sim_Ctx* sim);
void mel_unregister_sim(Mel_Sim_Ctx* sim);

void mel_frame(void);
Mel_Frame_Stats mel_frame_stats(void);
void mel_process_event(SDL_Event* event);

bool mel_imgui_init(Mel_Window_Handle window, Mel_Swapchain* swapchain);
