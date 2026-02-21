#pragma once

#include "swapchain.fwd.h"
#include "core.types.h"
#include "gpu.device.fwd.h"
#include "allocator.fwd.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef void (*Mel_Swapchain_Image_Present_Fn)(void* pixels, u32 width, u32 height, u32 stride, void* user_data);

typedef struct {
    u32 width;
    u32 height;
    u32 frame_count;
    VkFormat format;
    Mel_Swapchain_Image_Present_Fn on_present;
    void* user_data;
    const Mel_Alloc* alloc;
} Mel_Swapchain_Image_Opt;

bool mel_swapchain_image_init_opt(Mel_Swapchain* sc, Mel_Gpu_Device* dev, Mel_Swapchain_Image_Opt opt);
#define mel_swapchain_image_init(sc, dev, ...) mel_swapchain_image_init_opt((sc), (dev), (Mel_Swapchain_Image_Opt){__VA_ARGS__})
