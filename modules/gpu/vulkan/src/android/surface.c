#define VK_USE_PLATFORM_ANDROID_KHR

#include <android/native_window.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

VkSurfaceKHR mel_gpu__vk_create_android_surface(VkInstance instance, void* window)
{
    VkAndroidSurfaceCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = (struct ANativeWindow*)window,
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateAndroidSurfaceKHR(instance, &ci, NULL, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return surface;
}
