#pragma once

#include <gpu/types.h>

typedef struct {
    void*          native_window; // platform view the swapchain presents into (NSView* on macOS)
    i32            width, height;
    Mel_Gpu_Format format;        // UNDEFINED selects the backend default
    bool           vsync;
} Mel_Gpu_Swapchain_Opt;

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt);
#define mel_gpu_swapchain_create(dev, ...) \
    mel_gpu_swapchain_create_opt((dev), (Mel_Gpu_Swapchain_Opt){__VA_ARGS__})

void           mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc);
void           mel_gpu_swapchain_resize (Mel_Gpu_Swapchain* sc, i32 width, i32 height);
Mel_Gpu_Format mel_gpu_swapchain_format (const Mel_Gpu_Swapchain* sc);
