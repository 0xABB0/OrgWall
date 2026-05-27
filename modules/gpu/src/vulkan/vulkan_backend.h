#pragma once

#include <string.h>
#include <stdlib.h>

#ifdef __ANDROID__
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

#include <gpu/gpu.h>

#define MEL_GPU_VK_MAX_IMAGES 8

struct Mel_Gpu_Device {
    VkInstance       instance;
    VkPhysicalDevice phys;
    VkDevice         device;
    u32              queue_family;
    VkQueue          queue;
    VkCommandPool    cmd_pool;
};

struct Mel_Gpu_Buffer {
    Mel_Gpu_Device* device;
    VkBuffer        buf;
    VkDeviceMemory  mem;
    usize           size;
    bool            host_visible;
};

struct Mel_Gpu_Shader {
    Mel_Gpu_Device* device;
    VkShaderModule  vs;
    VkShaderModule  fs;
    char*           vertex_entry;
    char*           fragment_entry;
};

struct Mel_Gpu_Pipeline {
    Mel_Gpu_Device*  device;
    VkPipeline       pipeline;
    VkPipelineLayout layout;
};

struct Mel_Gpu_Command_List {
    Mel_Gpu_Swapchain* swapchain;
    VkCommandBuffer    cb;
};

struct Mel_Gpu_Swapchain {
    Mel_Gpu_Device* device;
    VkSurfaceKHR    surface;
    void*           metal_layer; // retained CAMetalLayer on macOS

    VkSwapchainKHR  swapchain;
    VkFormat        format;
    VkColorSpaceKHR color_space;
    Mel_Gpu_Format  mel_format;
    VkExtent2D      extent;
    i32             req_width, req_height;

    u32             image_count;
    VkImage         images[MEL_GPU_VK_MAX_IMAGES];
    VkImageView     views[MEL_GPU_VK_MAX_IMAGES];
    VkFramebuffer   framebuffers[MEL_GPU_VK_MAX_IMAGES];
    VkRenderPass    render_pass;

    VkCommandBuffer cmd_buffer;
    VkSemaphore     image_available;
    VkSemaphore     render_finished;
    VkFence         in_flight;

    u32             current_image;
    bool            frame_ok;
    Mel_Gpu_Command_List cmd;
};

// Implemented in pipeline.c: a single-color-attachment render pass.
VkRenderPass mel_gpu__vk_make_render_pass(VkDevice device, VkFormat format);

// Implemented in surface.m on Apple platforms.
void*    mel_gpu__vk_make_metal_layer(void* nsview);
void     mel_gpu__vk_release_metal_layer(void* layer);
void     mel_gpu__vk_layer_set_size(void* layer, i32 width, i32 height);
VkResult mel_gpu__vk_create_metal_surface(VkInstance instance, void* layer, VkSurfaceKHR* out_surface);

// Implemented in android_surface.c on Android (window is an ANativeWindow*).
VkResult mel_gpu__vk_create_android_surface(VkInstance instance, void* window, VkSurfaceKHR* out_surface);

static inline VkFormat mel_gpu__vk_color_format(Mel_Gpu_Format f)
{
    switch (f) {
        case MEL_GPU_FORMAT_RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case MEL_GPU_FORMAT_BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        default:                         return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

static inline Mel_Gpu_Format mel_gpu__vk_mel_format(VkFormat f)
{
    switch (f) {
        case VK_FORMAT_R8G8B8A8_UNORM: return MEL_GPU_FORMAT_RGBA8_UNORM;
        case VK_FORMAT_B8G8R8A8_UNORM: return MEL_GPU_FORMAT_BGRA8_UNORM;
        default:                       return MEL_GPU_FORMAT_BGRA8_UNORM;
    }
}

static inline VkFormat mel_gpu__vk_vertex_format(Mel_Gpu_Format f)
{
    switch (f) {
        case MEL_GPU_FORMAT_RG32_FLOAT:   return VK_FORMAT_R32G32_SFLOAT;
        case MEL_GPU_FORMAT_RGB32_FLOAT:  return VK_FORMAT_R32G32B32_SFLOAT;
        case MEL_GPU_FORMAT_RGBA32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        default:                          return VK_FORMAT_R32G32B32_SFLOAT;
    }
}

static inline VkPrimitiveTopology mel_gpu__vk_topology(Mel_Gpu_Topology t)
{
    switch (t) {
        case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case MEL_GPU_TOPOLOGY_LINE_LIST:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case MEL_GPU_TOPOLOGY_POINT_LIST:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:                              return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static inline VkCullModeFlags mel_gpu__vk_cull(Mel_Gpu_Cull c)
{
    switch (c) {
        case MEL_GPU_CULL_FRONT: return VK_CULL_MODE_FRONT_BIT;
        case MEL_GPU_CULL_BACK:  return VK_CULL_MODE_BACK_BIT;
        default:                 return VK_CULL_MODE_NONE;
    }
}
