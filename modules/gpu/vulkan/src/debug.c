#include "vk_backend.h"

#include <log/log.h>

static VKAPI_ATTR VkBool32 VKAPI_CALL mel_gpu__debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    (void)types;
    (void)user;
    const char* msg = data && data->pMessage ? data->pMessage : "(no message)";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        mel_log_error("gpu", "validation: %s", msg);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        mel_log_warn("gpu", "validation: %s", msg);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        mel_log_info("gpu", "validation: %s", msg);
    else
        mel_log_trace("gpu", "validation: %s", msg);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT mel_gpu__debug_messenger_info(void)
{
    return (VkDebugUtilsMessengerCreateInfoEXT){
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = mel_gpu__debug_cb,
    };
}

void mel_gpu__debug_messenger_create(VkInstance instance, VkDebugUtilsMessengerEXT* out)
{
    *out = VK_NULL_HANDLE;
    PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!fn)
        return;
    VkDebugUtilsMessengerCreateInfoEXT info = mel_gpu__debug_messenger_info();
    fn(instance, &info, NULL, out);
}

void mel_gpu__debug_messenger_destroy(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    if (messenger == VK_NULL_HANDLE)
        return;
    PFN_vkDestroyDebugUtilsMessengerEXT fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (fn)
        fn(instance, messenger, NULL);
}
