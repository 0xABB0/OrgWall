#pragma once

#include "gpu.device.h"
#include "gpu.backend.vulkan.h"

#include <vk_mem_alloc.h>
#include <SDL3/SDL_vulkan.h>

typedef struct Mel_Gpu_Device_Vulkan Mel_Gpu_Device_Vulkan;

struct Mel_Gpu_Device_Vulkan {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;
    VmaAllocator vma;
    VkPhysicalDeviceProperties2 device_properties;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT desc_buffer_props;
    bool has_descriptor_buffer;
    bool has_present_queue;
    bool has_portability_subset;
};

static inline Mel_Gpu_Device_Vulkan* mel__gpu_device_vk(Mel_Gpu_Device* dev)
{
    assert(dev != nullptr);
    assert(dev->_backend != nullptr);
    return (Mel_Gpu_Device_Vulkan*)dev->_backend;
}
