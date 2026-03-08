#pragma once

#include "swapchain.fwd.h"
#include "core.types.h"
#include "gpu.device.fwd.h"
#include "window.fwd.h"
#include "allocator.fwd.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef struct {
    VkSurfaceKHR surface;
    u32 width;
    u32 height;
    u32 frame_count;
    VkPresentModeKHR preferred_present_mode;
    const Mel_Alloc* alloc;
} Mel_Gpu_Swapchain_Opt;

bool mel_gpu_swapchain_init_opt(Mel_Swapchain* sc, Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt);
#define mel_gpu_swapchain_init(sc, dev, ...) mel_gpu_swapchain_init_opt((sc), (dev), (Mel_Gpu_Swapchain_Opt){__VA_ARGS__})

Mel_Swapchain_Handle mel_gpu_swapchain_create_for_window(Mel_Gpu_Device* dev, Mel_Window_Handle window);
