#ifdef __ANDROID__

#include "vulkan_backend.h"

VkResult mel_gpu__vk_create_android_surface(VkInstance instance, void* window, VkSurfaceKHR* out_surface)
{
    VkAndroidSurfaceCreateInfoKHR ci = {
        .sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = (struct ANativeWindow*)window,
    };
    return vkCreateAndroidSurfaceKHR(instance, &ci, NULL, out_surface);
}

#endif
