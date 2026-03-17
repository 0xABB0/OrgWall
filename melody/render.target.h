#pragma once

#include "render.target.fwd.h"
#include "gpu.image.h"
#include "swapchain.fwd.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"

#define MEL_RENDER_TARGET_COLOR     0
#define MEL_RENDER_TARGET_DEPTH     1
#define MEL_RENDER_TARGET_SWAPCHAIN 2

struct Mel_Render_Target {
    str8 name;
    u32 kind;
    u32 width;
    u32 height;
    Mel_Gpu_Format format;
    Mel_Gpu_Image image;
    Mel_Swapchain* swapchain;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
};

typedef struct {
    str8 name;
    u32 width;
    u32 height;
    Mel_Gpu_Format format;
    Mel_Gpu_Image_Usage extra_usage;
    const Mel_Alloc* alloc;
} Mel_Render_Target_Opt;

void mel_render_target_init_opt(Mel_Render_Target* t, Mel_Gpu_Device* dev, Mel_Render_Target_Opt opt);
#define mel_render_target_init(t, dev, ...) mel_render_target_init_opt((t), (dev), (Mel_Render_Target_Opt){__VA_ARGS__})

void mel_render_target_init_swapchain(Mel_Render_Target* t, Mel_Swapchain* swapchain, Mel_Gpu_Device* dev, str8 name);

void mel_render_target_shutdown(Mel_Render_Target* t);

void* mel_render_target_view(Mel_Render_Target* t);
void* mel_render_target_image(Mel_Render_Target* t);
u32 mel_render_target_width(Mel_Render_Target* t);
u32 mel_render_target_height(Mel_Render_Target* t);
