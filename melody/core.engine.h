#pragma once

#include "core.types.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"
#include "gpu.device.h"
#include "swapchain.h"
#include "gpu.cmd.fwd.h"
#include "render.frame.h"
#include "allocator.tracking.fwd.h"

#include <SDL3/SDL.h>

#define MEL_ENGINE_FEATURE_GPU   (1u << 0)
#define MEL_ENGINE_FEATURE_IMGUI (1u << 1)

typedef struct Mel_Engine Mel_Engine;

struct Mel_Engine {
    Mel_Gpu_Device dev;
    Mel_Swapchain swapchain;
    Mel_Render_Frame frame;
    Mel_Tracking_Allocator* tracking;
    Mel_Alloc allocator;
    VkDescriptorPool imgui_pool;
    SDL_Window* window;
    f32 fixed_dt;
    f32 max_frame_time;
    f32 accumulator;
    f32 frame_dt;
    f32 alpha;
    u64 last_time;
    u64 frame_count;
    bool resize_requested;
    bool imgui_initialized;
    u32 features;
};

typedef struct {
    SDL_Window* window;
    str8 app_name;
    const Mel_Alloc* allocator;
    bool enable_validation;
    bool enable_imgui;
    f32 fixed_dt;
    f32 max_frame_time;
} Mel_Engine_Opt;

typedef struct Mel_App Mel_App;

bool mel_engine_init_opt(Mel_Engine* engine, Mel_Engine_Opt opt);
#define mel_engine_init(engine, ...) mel_engine_init_opt((engine), (Mel_Engine_Opt){__VA_ARGS__})

void mel_engine_shutdown(Mel_Engine* engine);
void mel_engine_frame(Mel_Engine* engine, Mel_App* app);
void mel_engine_process_event(Mel_Engine* engine, SDL_Event* event);

typedef struct {
    f32 clear_r;
    f32 clear_g;
    f32 clear_b;
    f32 clear_a;
} Mel_Engine_Pass_Opt;

void mel_engine_begin_swapchain_pass_opt(Mel_Engine* engine, Mel_Gpu_Cmd* cmd, Mel_Engine_Pass_Opt opt);
#define mel_engine_begin_swapchain_pass(engine, cmd, ...) \
    mel_engine_begin_swapchain_pass_opt((engine), (cmd), (Mel_Engine_Pass_Opt){__VA_ARGS__})

void mel_engine_end_swapchain_pass(Mel_Engine* engine, Mel_Gpu_Cmd* cmd);

void mel__engine_init(void);
void mel__engine_shutdown(void);
