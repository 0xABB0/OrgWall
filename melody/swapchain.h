#pragma once

#include "swapchain.fwd.h"
#include "core.types.h"
#include "gpu.device.fwd.h"
#include "window.fwd.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef struct Mel_Gpu_Submit_Gather Mel_Gpu_Submit_Gather;

#define MEL_MAX_SWAPCHAINS 8

struct Mel_Gpu_Submit_Gather {
    VkSemaphore wait_semaphores[MEL_MAX_SWAPCHAINS];
    VkPipelineStageFlags wait_stages[MEL_MAX_SWAPCHAINS];
    u32 wait_count;
    VkSemaphore signal_semaphores[MEL_MAX_SWAPCHAINS];
    u32 signal_count;
};

struct Mel_Swapchain_Vtable {
    bool (*acquire)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    void (*prepare_present)(Mel_Swapchain* sc, VkCommandBuffer cmd);
    void (*collect_sync)(Mel_Swapchain* sc, Mel_Gpu_Submit_Gather* gather);
    void (*present)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    void (*resize)(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height);
    void (*shutdown)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    VkImageLayout (*current_image_layout)(Mel_Swapchain* sc);
    VkPresentModeKHR (*present_mode)(Mel_Swapchain* sc);
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

#define mel_swapchain_acquire(sc, dev)                    (sc)->vtable->acquire((sc), (dev))
#define mel_swapchain_prepare_present(sc, cmd)            do { if ((sc)->vtable->prepare_present) (sc)->vtable->prepare_present((sc), (cmd)); } while(0)
#define mel_swapchain_collect_sync(sc, gather)            do { if ((sc)->vtable->collect_sync) (sc)->vtable->collect_sync((sc), (gather)); } while(0)
#define mel_swapchain_present(sc, dev)                    do { if ((sc)->vtable->present) (sc)->vtable->present((sc), (dev)); } while(0)
#define mel_swapchain_resize(sc, dev, w, h)               (sc)->vtable->resize((sc), (dev), (w), (h))
#define mel_swapchain_shutdown(sc, dev)                    (sc)->vtable->shutdown((sc), (dev))

static inline VkImageLayout mel_swapchain_current_image_layout(Mel_Swapchain* sc)
{
    if (!sc->vtable->current_image_layout)
        return VK_IMAGE_LAYOUT_UNDEFINED;
    return sc->vtable->current_image_layout(sc);
}

static inline VkPresentModeKHR mel_swapchain_present_mode(Mel_Swapchain* sc)
{
    if (!sc->vtable->present_mode)
        return VK_PRESENT_MODE_FIFO_KHR;
    return sc->vtable->present_mode(sc);
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
