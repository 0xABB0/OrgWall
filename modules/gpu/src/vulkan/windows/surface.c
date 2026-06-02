#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

// U18 (gpu-rhi.md §7.4): the Win32 surface lowering. `hwnd` is the window module's native handle
// (modules/window/src/win32 stores the HWND as the node's `native`). Mirrors macos/surface.m — the only
// translation unit that pulls the platform Vulkan surface header, so windows.h stays out of the backend core.
VkSurfaceKHR mel_gpu__vk_create_win32_surface(VkInstance instance, void* hwnd)
{
    VkWin32SurfaceCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandleW(NULL),
        .hwnd = (HWND)hwnd,
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(instance, &ci, NULL, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return surface;
}
