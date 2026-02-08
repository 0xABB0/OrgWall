#ifndef MEL_VK_CONTEXT_H
#define MEL_VK_CONTEXT_H

#include "types.h"
#include "allocator.h"

#include <SDL3/SDL.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_vulkan.h>

typedef struct
{
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkSurfaceKHR surface;

    VkQueue graphics_queue;
    VkQueue present_queue;
    u32 graphics_family;
    u32 present_family;

    VmaAllocator vma;

    VkPhysicalDeviceProperties device_properties;
    VkPhysicalDeviceFeatures device_features;

    bool validation_enabled;
} Mel_VkContext;

typedef struct
{
    const Mel_Alloc* allocator;
    SDL_Window* window;
    bool enable_validation;
    const char* app_name;
} Mel_VkContext_Opt;

bool mel_vk_context_init_opt(Mel_VkContext* ctx, Mel_VkContext_Opt opt);
#define mel_vk_context_init(ctx, ...) mel_vk_context_init_opt((ctx), (Mel_VkContext_Opt){__VA_ARGS__})

void mel_vk_context_shutdown(Mel_VkContext* ctx);

void mel_vk_context_wait_idle(Mel_VkContext* ctx);

#endif
