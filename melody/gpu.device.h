#pragma once

#include "core.types.h"
#include "allocator.h"
#include "string.str8.fwd.h"

#include <SDL3/SDL.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_vulkan.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;
typedef struct Mel_Gpu_Capabilities Mel_Gpu_Capabilities;

struct Mel_Gpu_Capabilities {
    bool synchronization2;
    bool dynamic_rendering;
    bool buffer_device_address;
    bool descriptor_buffer;
    bool mesh_shader;
    bool multi_draw_indirect;
    bool timestamp_queries;
    bool portability_subset;
    bool present_queue;
};

struct Mel_Gpu_Device {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkDevice device;

    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;
    u32 graphics_family;
    u32 present_family;
    u32 transfer_family;

    VmaAllocator vma;

    VkPhysicalDeviceProperties2 device_properties;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT desc_buffer_props;
    Mel_Gpu_Capabilities capabilities;

    const Mel_Alloc* alloc;

    bool validation_enabled;
    bool has_descriptor_buffer;
    bool has_present_queue;
    bool has_portability_subset;
};

typedef struct {
    const Mel_Alloc* allocator;
    bool enable_validation;
    str8 app_name;
} Mel_Gpu_Device_Opt;

bool mel_gpu_device_init_opt(Mel_Gpu_Device* dev, Mel_Gpu_Device_Opt opt);
#define mel_gpu_device_init(dev, ...) mel_gpu_device_init_opt((dev), (Mel_Gpu_Device_Opt){__VA_ARGS__})

void mel_gpu_device_shutdown(Mel_Gpu_Device* dev);
void mel_gpu_device_wait_idle(Mel_Gpu_Device* dev);
Mel_Gpu_Capabilities mel_gpu_capabilities(Mel_Gpu_Device* dev);

VkSurfaceKHR mel_gpu_surface_create(Mel_Gpu_Device* dev, SDL_Window* window);
void mel_gpu_surface_destroy(Mel_Gpu_Device* dev, VkSurfaceKHR surface);
bool mel_gpu_device_configure_present(Mel_Gpu_Device* dev, VkSurfaceKHR surface);
