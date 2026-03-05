#pragma once

#include "core.types.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"
#include "gpu.device.h"
#include "swapchain.h"
#include "allocator.tracking.fwd.h"
#include "render.graph.fwd.h"
#include "sprite.pass.fwd.h"
#include "texture.pool.fwd.h"

#include <SDL3/SDL.h>

#define MEL_ENGINE_FEATURE_GPU   (1u << 0)
#define MEL_ENGINE_FEATURE_IMGUI (1u << 1)

typedef struct Mel_Engine Mel_Engine;

struct Mel_Engine {
    Mel_Gpu_Device dev;
    Mel_Swapchain swapchain;
    Mel_Tracking_Allocator* tracking;
    Mel_Alloc allocator;
    VkDescriptorPool imgui_pool;
    SDL_Window* window;
    f32 max_frame_time;
    f32 frame_dt;
    u64 last_time;
    u64 frame_count;
    Mel_Render_Graph* render_graph;
    Mel_Sprite_Pass* sprite_pass;
    Mel_Texture_Pool* texture_pool;
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
    f32 max_frame_time;
} Mel_Engine_Opt;

typedef struct Mel_App Mel_App;

bool mel_engine_init_opt(Mel_Engine* engine, Mel_Engine_Opt opt);
#define mel_engine_init(engine, ...) mel_engine_init_opt((engine), (Mel_Engine_Opt){__VA_ARGS__})

void mel_engine_shutdown(Mel_Engine* engine);
void mel_engine_frame(Mel_Engine* engine, Mel_App* app);
void mel_engine_process_event(Mel_Engine* engine, SDL_Event* event);

void mel__engine_init(void);
void mel__engine_shutdown(void);
