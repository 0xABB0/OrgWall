#include "surface.h"

#include <log/log.h>

typedef struct xcb_connection_t xcb_connection_t;
typedef u32                     xcb_window_t;
typedef u32                     xcb_visualid_t;

#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xcb.h>

static VkSurfaceKHR mel_gpu__vk_create_wayland_surface(VkInstance instance, void* display, void* surface)
{
    VkWaylandSurfaceCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = (struct wl_display*)display,
        .surface = (struct wl_surface*)surface,
    };
    VkSurfaceKHR out = VK_NULL_HANDLE;
    if (vkCreateWaylandSurfaceKHR(instance, &ci, NULL, &out) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return out;
}

static VkSurfaceKHR mel_gpu__vk_create_xcb_surface(VkInstance instance, void* connection, u32 window)
{
    VkXcbSurfaceCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = (xcb_connection_t*)connection,
        .window = (xcb_window_t)window,
    };
    VkSurfaceKHR out = VK_NULL_HANDLE;
    if (vkCreateXcbSurfaceKHR(instance, &ci, NULL, &out) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return out;
}

VkSurfaceKHR mel_gpu__vk_create_linux_surface(VkInstance instance, void* native)
{
    Mel_Gpu_Linux_Native* n = (Mel_Gpu_Linux_Native*)native;

    if (n->wl_display && n->wl_surface)
        return mel_gpu__vk_create_wayland_surface(instance, n->wl_display, n->wl_surface);

    if (n->xcb_connection && n->xcb_window)
        return mel_gpu__vk_create_xcb_surface(instance, n->xcb_connection, n->xcb_window);

    mel_log_error("gpu", "linux surface: native handle carries neither a Wayland (display+surface) nor an XCB (connection+window) pair");
    return VK_NULL_HANDLE;
}
