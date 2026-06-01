#pragma once

#include <core/types.h>
#include <gpu/format.h>
#include <gpu/surface.h>

typedef struct Mel_Gpu_Device    Mel_Gpu_Device;
typedef struct Mel_Gpu_Swapchain Mel_Gpu_Swapchain;

typedef struct
{
    Mel_Gpu_Surface* surface;
    i32              width;
    i32              height;
    Mel_Gpu_Format   format;
    bool             vsync;
} Mel_Gpu_Swapchain_Opt;

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt);
#define mel_gpu_swapchain_create(dev, ...) mel_gpu_swapchain_create_opt((dev), (Mel_Gpu_Swapchain_Opt){ __VA_ARGS__ })

void           mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc);
void           mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height);
Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc);
