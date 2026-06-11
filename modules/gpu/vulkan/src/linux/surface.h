#pragma once

#include <vulkan/vulkan.h>

#include <core/types.h>

typedef struct
{
    void* wl_display;
    void* wl_surface;

    void* xcb_connection;
    u32   xcb_window;
} Mel_Gpu_Linux_Native;

VkSurfaceKHR mel_gpu__vk_create_linux_surface(VkInstance instance, void* native);
