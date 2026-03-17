#pragma once

#include "core.types.h"
#include "sim.ctx.fwd.h"

#include <SDL3/SDL.h>

typedef struct {
    f32 dt;
    f32 fps;
    u64 frame_count;
} Mel_Frame_Stats;

void mel_boot(void);
void mel_shutdown(void);

void mel_register_sim(Mel_Sim_Ctx* sim);
void mel_unregister_sim(Mel_Sim_Ctx* sim);

void mel_frame(void);
Mel_Frame_Stats mel_frame_stats(void);
void mel_process_event(SDL_Event* event);
