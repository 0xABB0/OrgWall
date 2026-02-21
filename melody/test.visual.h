#pragma once

#include "core.types.h"
#include "gpu.device.h"
#include "gpu.cmd.h"
#include "swapchain.h"
#include "render.frame.h"

typedef void (*Mel_Visual_Test_Render_Fn)(Mel_Gpu_Cmd* cmd, Mel_Swapchain* sc, void* user_data);

typedef struct {
    bool passed;
    u32 diff_pixel_count;
    u32 total_pixels;
    f32 diff_percent;
} Mel_Visual_Test_Result;

typedef struct Mel_Visual_Test_Ctx Mel_Visual_Test_Ctx;

struct Mel_Visual_Test_Ctx {
    SDL_Window* window;
    Mel_Gpu_Device dev;
    Mel_Swapchain sc;
    Mel_Render_Frame frame;
    u32 width;
    u32 height;

    u8* captured_pixels;
    u32 captured_stride;
    bool has_capture;
};

typedef struct {
    u32 width;
    u32 height;
} Mel_Visual_Test_Init_Opt;

bool mel_visual_test_init_opt(Mel_Visual_Test_Ctx* ctx, Mel_Visual_Test_Init_Opt opt);
#define mel_visual_test_init(ctx, ...) mel_visual_test_init_opt((ctx), (Mel_Visual_Test_Init_Opt){__VA_ARGS__})

void mel_visual_test_shutdown(Mel_Visual_Test_Ctx* ctx);

Mel_Visual_Test_Result mel_visual_test_check(Mel_Visual_Test_Ctx* ctx, const char* test_name,
                                              Mel_Visual_Test_Render_Fn render_fn, void* user_data);
