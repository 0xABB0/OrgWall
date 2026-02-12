#ifndef MEL_GPU_DEVICE_H
#define MEL_GPU_DEVICE_H

#include "types.h"
#include "allocator.h"
#include "string.str8.fwd.h"

#include <SDL3/SDL.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_vulkan.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

struct Mel_Gpu_Device {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkSurfaceKHR surface;

    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;
    u32 graphics_family;
    u32 present_family;
    u32 transfer_family;

    VmaAllocator vma;

    VkPhysicalDeviceProperties2 device_properties;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT desc_buffer_props;

    const Mel_Alloc* alloc;

    bool validation_enabled;
    bool has_descriptor_buffer;
};

typedef struct {
    const Mel_Alloc* allocator;
    SDL_Window* window;
    bool enable_validation;
    str8 app_name;
} Mel_Gpu_Device_Opt;

bool mel_gpu_device_init_opt(Mel_Gpu_Device* dev, Mel_Gpu_Device_Opt opt);
#define mel_gpu_device_init(dev, ...) mel_gpu_device_init_opt((dev), (Mel_Gpu_Device_Opt){__VA_ARGS__})

void mel_gpu_device_shutdown(Mel_Gpu_Device* dev);
void mel_gpu_device_wait_idle(Mel_Gpu_Device* dev);

#endif
