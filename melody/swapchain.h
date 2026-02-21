#pragma once

#include "swapchain.fwd.h"
#include "core.types.h"
#include "gpu.device.fwd.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

struct Mel_Swapchain_Vtable {
    bool (*acquire)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    bool (*present)(Mel_Swapchain* sc, Mel_Gpu_Device* dev, VkCommandBuffer cmd, VkFence fence);
    void (*resize)(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height);
    void (*shutdown)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
};

struct Mel_Swapchain {
    const Mel_Swapchain_Vtable* vtable;
    void* data;

    VkFormat format;
    VkExtent2D extent;
    u32 image_count;
    u32 current_image;
    VkImage* images;
    VkImageView* image_views;
};

#define mel_swapchain_acquire(sc, dev)              (sc)->vtable->acquire((sc), (dev))
#define mel_swapchain_present(sc, dev, cmd, fence)  (sc)->vtable->present((sc), (dev), (cmd), (fence))
#define mel_swapchain_resize(sc, dev, w, h)         (sc)->vtable->resize((sc), (dev), (w), (h))
#define mel_swapchain_shutdown(sc, dev)              (sc)->vtable->shutdown((sc), (dev))
