#include "vulkan_backend.h"

#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif

static bool pick_queue_family(VkPhysicalDevice phys, u32* out_family)
{
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, NULL);
    if (count == 0) return false;
    VkQueueFamilyProperties props[16];
    if (count > 16) count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, props);
    for (u32 i = 0; i < count; i++) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { *out_family = i; return true; }
    }
    return false;
}

Mel_Gpu_Device* mel_gpu_device_create_opt(Mel_Gpu_Device_Opt opt)
{
    (void)opt;

    const char* inst_exts[8];
    u32         inst_ext_count = 0;
    inst_exts[inst_ext_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
#ifdef __APPLE__
    inst_exts[inst_ext_count++] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
    inst_exts[inst_ext_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    inst_exts[inst_ext_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif
#ifdef __ANDROID__
    inst_exts[inst_ext_count++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
#endif

    VkApplicationInfo app = {
        .sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "melody",
        .apiVersion = VK_API_VERSION_1_2,
    };
    VkInstanceCreateInfo ici = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app,
        .enabledExtensionCount   = inst_ext_count,
        .ppEnabledExtensionNames = inst_exts,
#ifdef __APPLE__
        .flags                   = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
#endif
    };

    VkInstance instance;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS) return NULL;

    u32 phys_count = 0;
    vkEnumeratePhysicalDevices(instance, &phys_count, NULL);
    if (phys_count == 0) { vkDestroyInstance(instance, NULL); return NULL; }
    if (phys_count > 8) phys_count = 8;
    VkPhysicalDevice physs[8];
    vkEnumeratePhysicalDevices(instance, &phys_count, physs);
    VkPhysicalDevice phys = physs[0];

    u32 family;
    if (!pick_queue_family(phys, &family)) { vkDestroyInstance(instance, NULL); return NULL; }

    const char* dev_exts[4];
    u32         dev_ext_count = 0;
    dev_exts[dev_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
#ifdef __APPLE__
    dev_exts[dev_ext_count++] = "VK_KHR_portability_subset";
#endif

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount       = 1,
        .pQueuePriorities = &prio,
    };
    VkDeviceCreateInfo dci = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &qci,
        .enabledExtensionCount   = dev_ext_count,
        .ppEnabledExtensionNames = dev_exts,
    };

    VkDevice device;
    if (vkCreateDevice(phys, &dci, NULL, &device) != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return NULL;
    }

    VkCommandPoolCreateInfo pci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = family,
    };
    VkCommandPool pool;
    if (vkCreateCommandPool(device, &pci, NULL, &pool) != VK_SUCCESS) {
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return NULL;
    }

    Mel_Gpu_Device* dev = calloc(1, sizeof *dev);
    if (!dev) return NULL;
    dev->instance     = instance;
    dev->phys         = phys;
    dev->device       = device;
    dev->queue_family = family;
    dev->cmd_pool     = pool;
    vkGetDeviceQueue(device, family, 0, &dev->queue);
    return dev;
}

void mel_gpu_device_destroy(Mel_Gpu_Device* dev)
{
    if (!dev) return;
    if (dev->device) vkDeviceWaitIdle(dev->device);
    if (dev->cmd_pool) vkDestroyCommandPool(dev->device, dev->cmd_pool, NULL);
    if (dev->device)   vkDestroyDevice(dev->device, NULL);
    if (dev->instance) vkDestroyInstance(dev->instance, NULL);
    free(dev);
}
