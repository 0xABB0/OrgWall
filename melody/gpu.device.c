#define VK_NO_PROTOTYPES
#include "gpu.device.h"
#include "string.str8.h"
#include "allocator.heap.h"
#include <string.h>

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    MEL_UNUSED(user_data);

    const char* severity_str = "UNKNOWN";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) severity_str = "ERROR";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) severity_str = "WARNING";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) severity_str = "INFO";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) severity_str = "VERBOSE";

    const char* type_str = "";
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) type_str = "VALIDATION";
    else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) type_str = "PERFORMANCE";
    else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) type_str = "GENERAL";

    SDL_Log("[Vulkan %s %s] %s", severity_str, type_str, callback_data->pMessage);

    return VK_FALSE;
}

static bool check_validation_support(void)
{
    u32 layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    VkLayerProperties* layers = mel_alloc(mel_alloc_heap(), sizeof(VkLayerProperties) * layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layers);

    bool found = false;
    for (u32 i = 0; i < layer_count; i++)
    {
        if (strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            found = true;
            break;
        }
    }

    mel_dealloc(mel_alloc_heap(), layers);
    return found;
}

static bool has_extension(VkExtensionProperties* exts, u32 count, const char* name)
{
    for (u32 i = 0; i < count; i++)
    {
        if (strcmp(exts[i].extensionName, name) == 0)
            return true;
    }
    return false;
}

static void create_instance(Mel_Gpu_Device* dev, Mel_Gpu_Device_Opt* opt)
{
    char app_name_buf[256];
    if (!str8_is_empty(opt->app_name))
        str8_to_buf(opt->app_name, app_name_buf, sizeof(app_name_buf));
    else
        strncpy(app_name_buf, "Melody", sizeof(app_name_buf));

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = app_name_buf,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Melody",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    u32 available_ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &available_ext_count, nullptr);
    VkExtensionProperties* available_exts = mel_alloc(mel_alloc_heap(), sizeof(VkExtensionProperties) * available_ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &available_ext_count, available_exts);

    bool has_portability_enum = has_extension(available_exts, available_ext_count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    mel_dealloc(mel_alloc_heap(), available_exts);

    u32 sdl_ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);

    u32 ext_count = 0;
    const char* extensions[32];

    for (u32 i = 0; i < sdl_ext_count; i++)
    {
        if (!has_portability_enum && strcmp(sdl_exts[i], VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
            continue;
        extensions[ext_count++] = sdl_exts[i];
    }

    if (opt->enable_validation)
        extensions[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    u32 layer_count = 0;

    if (opt->enable_validation && check_validation_support())
    {
        layer_count = 1;
        dev->validation_enabled = true;
    }

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = extensions,
        .enabledLayerCount = layer_count,
        .ppEnabledLayerNames = layers,
        .flags = has_portability_enum ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0,
    };

    VkResult r = vkCreateInstance(&create_info, nullptr, &dev->instance);
    assert(r == VK_SUCCESS);

    volkLoadInstance(dev->instance);
}

static void create_debug_messenger(Mel_Gpu_Device* dev)
{
    if (!dev->validation_enabled) return;

    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };

    VkResult r = vkCreateDebugUtilsMessengerEXT(dev->instance, &create_info, nullptr, &dev->debug_messenger);
    assert(r == VK_SUCCESS);
}

static bool find_queue_families(Mel_Gpu_Device* dev, VkPhysicalDevice device,
                                u32* graphics, u32* present, u32* transfer)
{
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

    VkQueueFamilyProperties* props = mel_alloc(mel_alloc_heap(), sizeof(VkQueueFamilyProperties) * count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

    bool found_graphics = false;
    bool found_present = false;
    bool found_transfer = false;

    for (u32 i = 0; i < count; i++)
    {
        if (!found_graphics && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            *graphics = i;
            found_graphics = true;
        }

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, dev->surface, &present_support);
        if (!found_present && present_support)
        {
            *present = i;
            found_present = true;
        }

        if (!found_transfer && (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            *transfer = i;
            found_transfer = true;
        }
    }

    if (!found_transfer)
    {
        *transfer = *graphics;
        found_transfer = true;
    }

    mel_dealloc(mel_alloc_heap(), props);
    return found_graphics && found_present;
}

static i32 rate_device(Mel_Gpu_Device* dev, VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    if (props.apiVersion < VK_API_VERSION_1_3)
        return -1;

    u32 graphics, present, transfer;
    if (!find_queue_families(dev, device, &graphics, &present, &transfer))
        return -1;

    u32 ext_count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr);
    VkExtensionProperties* exts = mel_alloc(mel_alloc_heap(), sizeof(VkExtensionProperties) * ext_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, exts);

    bool has_swapchain = has_extension(exts, ext_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    mel_dealloc(mel_alloc_heap(), exts);

    if (!has_swapchain) return -1;

    i32 score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;

    score += (i32)(props.limits.maxImageDimension2D / 1000);

    return score;
}

static void pick_physical_device(Mel_Gpu_Device* dev)
{
    u32 count = 0;
    vkEnumeratePhysicalDevices(dev->instance, &count, nullptr);
    assert(count > 0 && "No Vulkan devices found");

    VkPhysicalDevice* devices = mel_alloc(mel_alloc_heap(), sizeof(VkPhysicalDevice) * count);
    vkEnumeratePhysicalDevices(dev->instance, &count, devices);

    i32 best_score = -1;
    VkPhysicalDevice best_device = VK_NULL_HANDLE;

    for (u32 i = 0; i < count; i++)
    {
        i32 score = rate_device(dev, devices[i]);
        if (score > best_score)
        {
            best_score = score;
            best_device = devices[i];
        }
    }

    mel_dealloc(mel_alloc_heap(), devices);
    assert(best_device != VK_NULL_HANDLE && "No suitable Vulkan device found");

    dev->physical_device = best_device;

    dev->desc_buffer_props = (VkPhysicalDeviceDescriptorBufferPropertiesEXT){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT,
    };

    dev->device_properties = (VkPhysicalDeviceProperties2){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &dev->desc_buffer_props,
    };

    vkGetPhysicalDeviceProperties2(dev->physical_device, &dev->device_properties);

    SDL_Log("Selected GPU: %s", dev->device_properties.properties.deviceName);
}

static void create_logical_device(Mel_Gpu_Device* dev)
{
    find_queue_families(dev, dev->physical_device,
        &dev->graphics_family, &dev->present_family, &dev->transfer_family);

    f32 priority = 1.0f;

    u32 unique_families[3];
    u32 unique_count = 0;
    unique_families[unique_count++] = dev->graphics_family;
    if (dev->present_family != dev->graphics_family)
        unique_families[unique_count++] = dev->present_family;
    if (dev->transfer_family != dev->graphics_family && dev->transfer_family != dev->present_family)
        unique_families[unique_count++] = dev->transfer_family;

    VkDeviceQueueCreateInfo queue_infos[3];
    for (u32 i = 0; i < unique_count; i++)
    {
        queue_infos[i] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = unique_families[i],
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
    }

    u32 ext_count;
    vkEnumerateDeviceExtensionProperties(dev->physical_device, nullptr, &ext_count, nullptr);
    VkExtensionProperties* available_exts = mel_alloc(mel_alloc_heap(), sizeof(VkExtensionProperties) * ext_count);
    vkEnumerateDeviceExtensionProperties(dev->physical_device, nullptr, &ext_count, available_exts);

    dev->has_descriptor_buffer = has_extension(available_exts, ext_count, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
    bool has_portability = has_extension(available_exts, ext_count, "VK_KHR_portability_subset");
    mel_dealloc(mel_alloc_heap(), available_exts);

    u32 enabled_ext_count = 0;
    const char* enabled_exts[8];
    enabled_exts[enabled_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (has_portability)
        enabled_exts[enabled_ext_count++] = "VK_KHR_portability_subset";
    if (dev->has_descriptor_buffer)
        enabled_exts[enabled_ext_count++] = VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME;

    VkPhysicalDeviceBufferDeviceAddressFeatures bda_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .bufferDeviceAddress = VK_TRUE,
    };

    VkPhysicalDeviceSynchronization2Features sync2_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = &bda_features,
        .synchronization2 = VK_TRUE,
    };

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &sync2_features,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &dynamic_rendering_features,
    };

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features2,
        .queueCreateInfoCount = unique_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = enabled_ext_count,
        .ppEnabledExtensionNames = enabled_exts,
    };

    VkResult r = vkCreateDevice(dev->physical_device, &create_info, nullptr, &dev->device);
    assert(r == VK_SUCCESS);

    volkLoadDevice(dev->device);

    vkGetDeviceQueue(dev->device, dev->graphics_family, 0, &dev->graphics_queue);
    vkGetDeviceQueue(dev->device, dev->present_family, 0, &dev->present_queue);
    vkGetDeviceQueue(dev->device, dev->transfer_family, 0, &dev->transfer_queue);
}

static void create_vma(Mel_Gpu_Device* dev)
{
    VmaVulkanFunctions funcs = {
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo create_info = {
        .flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = dev->physical_device,
        .device = dev->device,
        .instance = dev->instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
        .pVulkanFunctions = &funcs,
    };

    VkResult r = vmaCreateAllocator(&create_info, &dev->vma);
    assert(r == VK_SUCCESS);
}

void mel_gpu_device_init_opt(Mel_Gpu_Device* dev, Mel_Gpu_Device_Opt opt)
{
    assert(dev != nullptr);
    assert(opt.window != nullptr);

    *dev = (Mel_Gpu_Device){0};
    dev->alloc = opt.allocator;

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_func =
        (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    assert(vkGetInstanceProcAddr_func != nullptr);

    volkInitializeCustom(vkGetInstanceProcAddr_func);

    create_instance(dev, &opt);
    create_debug_messenger(dev);

    bool surface_ok = SDL_Vulkan_CreateSurface(opt.window, dev->instance, nullptr, &dev->surface);
    assert(surface_ok && "Failed to create Vulkan surface");

    pick_physical_device(dev);
    create_logical_device(dev);
    create_vma(dev);

    SDL_Log("Vulkan device initialized (Vulkan 1.3, sync2, dynamic rendering, BDA%s)",
        dev->has_descriptor_buffer ? ", descriptor buffer" : "");
}

void mel_gpu_device_shutdown(Mel_Gpu_Device* dev)
{
    assert(dev != nullptr);

    if (dev->device) vkDeviceWaitIdle(dev->device);
    if (dev->vma) vmaDestroyAllocator(dev->vma);
    if (dev->device) vkDestroyDevice(dev->device, nullptr);
    if (dev->surface) vkDestroySurfaceKHR(dev->instance, dev->surface, nullptr);
    if (dev->debug_messenger) vkDestroyDebugUtilsMessengerEXT(dev->instance, dev->debug_messenger, nullptr);
    if (dev->instance) vkDestroyInstance(dev->instance, nullptr);

    SDL_Log("Vulkan device shutdown");
}

void mel_gpu_device_wait_idle(Mel_Gpu_Device* dev)
{
    assert(dev != nullptr);
    vkDeviceWaitIdle(dev->device);
}
