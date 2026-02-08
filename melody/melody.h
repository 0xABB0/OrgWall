#ifndef MELODY_H
#define MELODY_H

#include "types.h"
#include "allocator.fwd.h"
#include "vk_context.h"
#include "vk_swapchain.h"
#include "vk_shader.h"
#include <SDL3/SDL.h>

#define MEL_FIXED_DT (1.0f / 60.0f)
#define MEL_MAX_FRAME_TIME 0.25f

typedef struct Mel_Engine Mel_Engine;
#include "allocator.tracking.fwd.h"

typedef void (*Mel_Init_Func)(Mel_Engine* engine);
typedef void (*Mel_Shutdown_Func)(Mel_Engine* engine);
typedef void (*Mel_Update_Func)(Mel_Engine* engine, f32 dt);
typedef void (*Mel_Render_Func)(Mel_Engine* engine, f32 alpha);
typedef void (*Mel_Event_Func)(Mel_Engine* engine, SDL_Event* event);

typedef struct
{
    const char* title;
    i32 width;
    i32 height;
    Mel_Init_Func on_init;
    Mel_Shutdown_Func on_shutdown;
    Mel_Update_Func on_update;
    Mel_Render_Func on_render;
    Mel_Event_Func on_event;
} Mel_Engine_Opt;

struct Mel_Engine
{
    SDL_Window* window;

    Mel_VkContext vk;
    Mel_VkSwapchain swapchain;

    Mel_Tracking_Allocator* tracking;
    Mel_Alloc allocator;

    Mel_Engine_Opt opt;

    f32 accumulator;
    u64 last_time;
    f32 frame_dt;
    f32 fixed_dt;
    u64 frame_count;

    bool running;
    bool should_quit;
    bool resize_requested;
};

bool mel_engine_init_opt(Mel_Engine* engine, Mel_Engine_Opt opt);
#define mel_engine_init(engine, ...) mel_engine_init_opt((engine), (Mel_Engine_Opt){__VA_ARGS__})

void mel_engine_shutdown(Mel_Engine* engine);
bool mel_engine_handle_event(Mel_Engine* engine, SDL_Event* event);
void mel_engine_iterate(Mel_Engine* engine);

[[nodiscard]] f32 mel_engine_get_fps(Mel_Engine* engine);
[[nodiscard]] const Mel_Alloc* mel_engine_allocator(Mel_Engine* engine);

#endif
