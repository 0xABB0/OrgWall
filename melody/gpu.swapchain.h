#ifndef MEL_GPU_SWAPCHAIN_H
#define MEL_GPU_SWAPCHAIN_H

#include "gpu.device.h"

typedef struct Mel_Gpu_Swapchain Mel_Gpu_Swapchain;

struct Mel_Gpu_Swapchain {
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    VkColorSpaceKHR color_space;
    VkPresentModeKHR present_mode;

    u32 image_count;
    VkImage* images;
    VkImageView* image_views;

    u32 current_image;
    const Mel_Alloc* alloc;
};

typedef struct {
    u32 width;
    u32 height;
    VkPresentModeKHR preferred_present_mode;
    const Mel_Alloc* alloc;
} Mel_Gpu_Swapchain_Opt;

bool mel_gpu_swapchain_init_opt(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt);
#define mel_gpu_swapchain_init(sc, dev, ...) mel_gpu_swapchain_init_opt((sc), (dev), (Mel_Gpu_Swapchain_Opt){__VA_ARGS__})

void mel_gpu_swapchain_shutdown(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev);
void mel_gpu_swapchain_recreate(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height);

bool mel_gpu_swapchain_acquire(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, VkSemaphore signal_semaphore);
bool mel_gpu_swapchain_present(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, VkSemaphore wait_semaphore);

#endif
