#pragma once

#include "core.types.h"
#include "gpu.device.h"
#include "swapchain.h"
#include "render.target.h"
#include "render.pass.fwd.h"

typedef void (*Mel_Visual_Test_Render_Fn)(Mel_Render_Pass_Ctx* ctx);

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
    Mel_Render_Target target;
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

typedef struct {
    f32 clear_r;
    f32 clear_g;
    f32 clear_b;
    f32 clear_a;
} Mel_Visual_Test_Check_Opt;

Mel_Visual_Test_Result mel_visual_test_check_opt(Mel_Visual_Test_Ctx* ctx, const char* test_name,
                                                  Mel_Visual_Test_Render_Fn render_fn, Mel_Visual_Test_Check_Opt opt);
#define mel_visual_test_check(ctx, name, fn, ...) \
    mel_visual_test_check_opt((ctx), (name), (fn), (Mel_Visual_Test_Check_Opt){__VA_ARGS__})
