#pragma once

#include "render.target.fwd.h"
#include "gpu.types.h"
#include "gpu.image.h"
#include "gpu.device.fwd.h"
#include "swapchain.fwd.h"
#include "allocator.fwd.h"

#define MEL_TARGET_WINDOW    0
#define MEL_TARGET_OFFSCREEN 1
#define MEL_TARGET_ARRAY     2

struct Mel_Render_Target {
    u32               type;
    u32               width;
    u32               height;
    Mel_Gpu_Format    format;
    Mel_Swapchain_Handle swapchain;
    Mel_Gpu_Image     offscreen_image;
    void*             _current_view;
    Mel_Gpu_Device*   _dev;
    const Mel_Alloc*  _alloc;
};

typedef struct {
    u32              width;
    u32              height;
    Mel_Gpu_Format   format;
    Mel_Gpu_Device*  dev;
    const Mel_Alloc* alloc;
} Mel_Render_Target_Offscreen_Opt;

Mel_Render_Target* mel_render_target_from_swapchain(Mel_Swapchain_Handle sc, const Mel_Alloc* alloc);

Mel_Render_Target* mel_render_target_offscreen_opt(Mel_Render_Target_Offscreen_Opt opt);
#define mel_render_target_offscreen(...) mel_render_target_offscreen_opt((Mel_Render_Target_Offscreen_Opt){__VA_ARGS__})

void mel_render_target_destroy(Mel_Render_Target* target);

u32            mel_render_target_width(Mel_Render_Target* target);
u32            mel_render_target_height(Mel_Render_Target* target);
Mel_Gpu_Format mel_render_target_format(Mel_Render_Target* target);
void*          mel_render_target_image_view(Mel_Render_Target* target);

void mel_render_target_begin_frame(Mel_Render_Target* target);
