#define VK_NO_PROTOTYPES
#include "vk_context.h"
#include "memory.h"
#include <string.h>

#define VK_CHECK(expr) do { \
    VkResult _res = (expr); \
    if (_res != VK_SUCCESS) { \
        SDL_Log("Vulkan error: %s returned %d", #expr, (int)_res); \
    } \
    assert(_res == VK_SUCCESS && #expr); \
} while (0)

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

    VkLayerProperties* layers = mel_malloc(mel_alloc_malloc(), sizeof(VkLayerProperties) * layer_count);
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

    mel_free(mel_alloc_malloc(), layers);
    return found;
}

static bool create_instance(Mel_VkContext* ctx, Mel_VkContext_Opt* opt)
{
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = opt->app_name ? opt->app_name : "Melody",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Melody",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    u32 available_ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &available_ext_count, nullptr);
    VkExtensionProperties* available_exts = mel_malloc(mel_alloc_malloc(), sizeof(VkExtensionProperties) * available_ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &available_ext_count, available_exts);

    bool has_portability_enum = false;
    for (u32 i = 0; i < available_ext_count; i++)
    {
        if (strcmp(available_exts[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
        {
            has_portability_enum = true;
            break;
        }
    }
    mel_free(mel_alloc_malloc(), available_exts);

    u32 sdl_ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);

    u32 ext_count = 0;
    const char* extensions[32];

    for (u32 i = 0; i < sdl_ext_count; i++)
    {
        if (!has_portability_enum && strcmp(sdl_exts[i], VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
        {
            continue;
        }
        extensions[ext_count++] = sdl_exts[i];
    }

    if (opt->enable_validation)
    {
        extensions[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    u32 layer_count = 0;

    if (opt->enable_validation && check_validation_support())
    {
        layer_count = 1;
        ctx->validation_enabled = true;
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

    VK_CHECK(vkCreateInstance(&create_info, nullptr, &ctx->instance));

    volkLoadInstance(ctx->instance);

    return true;
}

static bool create_debug_messenger(Mel_VkContext* ctx)
{
    if (!ctx->validation_enabled) return true;

    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };

    VK_CHECK(vkCreateDebugUtilsMessengerEXT(ctx->instance, &create_info, nullptr, &ctx->debug_messenger));

    return true;
}

static bool find_queue_families(Mel_VkContext* ctx, VkPhysicalDevice device, u32* graphics, u32* present)
{
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

    VkQueueFamilyProperties* props = mel_malloc(mel_alloc_malloc(), sizeof(VkQueueFamilyProperties) * count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

    bool found_graphics = false;
    bool found_present = false;

    for (u32 i = 0; i < count; i++)
    {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            *graphics = i;
            found_graphics = true;
        }

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, ctx->surface, &present_support);
        if (present_support)
        {
            *present = i;
            found_present = true;
        }

        if (found_graphics && found_present) break;
    }

    mel_free(mel_alloc_malloc(), props);
    return found_graphics && found_present;
}

static bool check_device_extensions(VkPhysicalDevice device)
{
    u32 count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);

    VkExtensionProperties* exts = mel_malloc(mel_alloc_malloc(), sizeof(VkExtensionProperties) * count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, exts);

    bool has_swapchain = false;
    for (u32 i = 0; i < count; i++)
    {
        if (strcmp(exts[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
        {
            has_swapchain = true;
            break;
        }
    }

    mel_free(mel_alloc_malloc(), exts);
    return has_swapchain;
}

static i32 rate_device(Mel_VkContext* ctx, VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    u32 graphics, present;
    if (!find_queue_families(ctx, device, &graphics, &present)) return -1;
    if (!check_device_extensions(device)) return -1;

    i32 score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;

    score += (i32)(props.limits.maxImageDimension2D / 1000);

    return score;
}

static bool pick_physical_device(Mel_VkContext* ctx)
{
    u32 count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &count, nullptr);
    assert(count > 0 && "No Vulkan devices found");

    VkPhysicalDevice* devices = mel_malloc(mel_alloc_malloc(), sizeof(VkPhysicalDevice) * count);
    vkEnumeratePhysicalDevices(ctx->instance, &count, devices);

    i32 best_score = -1;
    VkPhysicalDevice best_device = VK_NULL_HANDLE;

    for (u32 i = 0; i < count; i++)
    {
        i32 score = rate_device(ctx, devices[i]);
        if (score > best_score)
        {
            best_score = score;
            best_device = devices[i];
        }
    }

    mel_free(mel_alloc_malloc(), devices);

    if (best_device == VK_NULL_HANDLE)
    {
        SDL_Log("No suitable Vulkan device found");
        return false;
    }

    ctx->physical_device = best_device;
    vkGetPhysicalDeviceProperties(best_device, &ctx->device_properties);
    vkGetPhysicalDeviceFeatures(best_device, &ctx->device_features);

    SDL_Log("Selected GPU: %s", ctx->device_properties.deviceName);

    return true;
}

static bool create_logical_device(Mel_VkContext* ctx)
{
    find_queue_families(ctx, ctx->physical_device, &ctx->graphics_family, &ctx->present_family);

    f32 priority = 1.0f;
    u32 unique_families[2] = { ctx->graphics_family, ctx->present_family };
    u32 unique_count = (ctx->graphics_family == ctx->present_family) ? 1 : 2;

    VkDeviceQueueCreateInfo queue_infos[2];
    for (u32 i = 0; i < unique_count; i++)
    {
        queue_infos[i] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = unique_families[i],
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
    }

    VkPhysicalDeviceFeatures features = {0};

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .dynamicRendering = VK_TRUE,
    };

    const char* extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset",
    };

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &dynamic_rendering_features,
        .queueCreateInfoCount = unique_count,
        .pQueueCreateInfos = queue_infos,
        .pEnabledFeatures = &features,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = extensions,
    };

    VK_CHECK(vkCreateDevice(ctx->physical_device, &create_info, nullptr, &ctx->device));

    volkLoadDevice(ctx->device);

    vkGetDeviceQueue(ctx->device, ctx->graphics_family, 0, &ctx->graphics_queue);
    vkGetDeviceQueue(ctx->device, ctx->present_family, 0, &ctx->present_queue);

    return true;
}

static bool create_vma(Mel_VkContext* ctx)
{
    VmaVulkanFunctions funcs = {
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo create_info = {
        .flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT,
        .physicalDevice = ctx->physical_device,
        .device = ctx->device,
        .instance = ctx->instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
        .pVulkanFunctions = &funcs,
    };

    VK_CHECK(vmaCreateAllocator(&create_info, &ctx->vma));

    return true;
}

bool mel_vk_context_init_opt(Mel_VkContext* ctx, Mel_VkContext_Opt opt)
{
    assert(ctx != nullptr);
    assert(opt.window != nullptr);

    *ctx = (Mel_VkContext){0};

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_func =
        (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (!vkGetInstanceProcAddr_func)
    {
        SDL_Log("Failed to get vkGetInstanceProcAddr from SDL");
        return false;
    }

    volkInitializeCustom(vkGetInstanceProcAddr_func);

    if (!create_instance(ctx, &opt)) return false;

    if (!create_debug_messenger(ctx)) return false;

    if (!SDL_Vulkan_CreateSurface(opt.window, ctx->instance, nullptr, &ctx->surface))
    {
        SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
        return false;
    }

    if (!pick_physical_device(ctx)) return false;
    if (!create_logical_device(ctx)) return false;
    if (!create_vma(ctx)) return false;

    SDL_Log("Vulkan context initialized");

    return true;
}

void mel_vk_context_shutdown(Mel_VkContext* ctx)
{
    assert(ctx != nullptr);

    if (ctx->device)
    {
        vkDeviceWaitIdle(ctx->device);
    }

    if (ctx->vma)
    {
        vmaDestroyAllocator(ctx->vma);
    }

    if (ctx->device)
    {
        vkDestroyDevice(ctx->device, nullptr);
    }

    if (ctx->surface)
    {
        vkDestroySurfaceKHR(ctx->instance, ctx->surface, nullptr);
    }

    if (ctx->debug_messenger)
    {
        vkDestroyDebugUtilsMessengerEXT(ctx->instance, ctx->debug_messenger, nullptr);
    }

    if (ctx->instance)
    {
        vkDestroyInstance(ctx->instance, nullptr);
    }

    SDL_Log("Vulkan context shutdown");
}

void mel_vk_context_wait_idle(Mel_VkContext* ctx)
{
    assert(ctx != nullptr);
    vkDeviceWaitIdle(ctx->device);
}
