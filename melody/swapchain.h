#pragma once

#include "swapchain.fwd.h"
#include "core.types.h"
#include "gpu.device.fwd.h"
#include "window.fwd.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

struct Mel_Swapchain_Vtable {
    bool (*acquire)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    bool (*present)(Mel_Swapchain* sc, Mel_Gpu_Device* dev, VkCommandBuffer cmd, VkFence fence);
    void (*resize)(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height);
    void (*shutdown)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    VkImageLayout (*current_image_layout)(Mel_Swapchain* sc);
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

static inline VkImageLayout mel_swapchain_current_image_layout(Mel_Swapchain* sc)
{
    if (!sc->vtable->current_image_layout)
        return VK_IMAGE_LAYOUT_UNDEFINED;
    return sc->vtable->current_image_layout(sc);
}

struct Mel_Swapchain_Entry {
    Mel_Swapchain swapchain;
    VkSurfaceKHR surface;
    Mel_Window_Handle window;
    bool resize_requested;
};

Mel_Swapchain_Handle mel_swapchain_registry_insert(Mel_Swapchain_Entry* entry);
void                 mel_swapchain_registry_remove(Mel_Swapchain_Handle handle, Mel_Gpu_Device* dev);
Mel_Swapchain_Entry* mel_swapchain_registry_get(Mel_Swapchain_Handle handle);
Mel_Swapchain_Handle mel_swapchain_registry_find_by_window(Mel_Window_Handle window);
u32                  mel_swapchain_registry_count(void);
void                 mel_swapchain_registry_destroy_all(Mel_Gpu_Device* dev);
