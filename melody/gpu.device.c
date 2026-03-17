#define VK_NO_PROTOTYPES
#include "gpu.device.h"
#include "gpu.backend.vulkan.h"
#include "event.channel.h"
#include "string.str8.h"
#include "allocator.heap.h"
#include <string.h>

static Mel_Gpu_Device s_dev;

Mel_Gpu_Device* mel_gpu_dev(void)
{
    return &s_dev;
}

Mel_Event_Channel mel_gpu_device_ready;

__attribute__((constructor))
static void mel__gpu_device_register(void)
{
    mel_event_channel_init(&mel_gpu_device_ready, mel_alloc_heap());
}

__attribute__((destructor))
static void mel__gpu_device_unregister(void)
{
    mel_event_channel_destroy(&mel_gpu_device_ready);
}

static i32 rate_device(VkPhysicalDevice device);

static const char* device_type_name(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

static void log_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    if (count == 0)
        return;

    VkQueueFamilyProperties* props = mel_alloc(mel_alloc_heap(), sizeof(VkQueueFamilyProperties) * count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

    for (u32 i = 0; i < count; i++)
    {
        VkBool32 present_support = VK_FALSE;
        if (surface != VK_NULL_HANDLE)
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);

        SDL_Log("Queue family %u: count=%u flags=%s%s%s%s present=%s",
            i,
            props[i].queueCount,
            (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? "graphics " : "",
            (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) ? "compute " : "",
            (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) ? "transfer " : "",
            (props[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) ? "sparse " : "",
            present_support ? "yes" : "no");
    }

    mel_dealloc(mel_alloc_heap(), props);
}

static void log_memory_heaps(VkPhysicalDevice device)
{
    VkPhysicalDeviceMemoryProperties mem = {0};
    vkGetPhysicalDeviceMemoryProperties(device, &mem);

    for (u32 i = 0; i < mem.memoryHeapCount; i++)
    {
        f64 gib = (f64)mem.memoryHeaps[i].size / (1024.0 * 1024.0 * 1024.0);
        SDL_Log("Memory heap %u: %.2f GiB flags=%s%s",
            i,
            gib,
            (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "device_local " : "",
            (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT) ? "multi_instance " : "");
    }
}

static void log_available_devices(VkPhysicalDevice* devices, u32 count)
{
    for (u32 i = 0; i < count; i++)
    {
        VkPhysicalDeviceProperties props = {0};
        vkGetPhysicalDeviceProperties(devices[i], &props);
        i32 score = rate_device(devices[i]);
        SDL_Log("GPU candidate %u: %s (%s, Vulkan %u.%u.%u, score=%d)",
            i,
            props.deviceName,
            device_type_name(props.deviceType),
            VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion),
            VK_API_VERSION_PATCH(props.apiVersion),
            score);
    }
}

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

static bool create_instance(Mel_Gpu_Device* dev, Mel_Gpu_Device_Opt* opt)
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
    if (r != VK_SUCCESS)
    {
        SDL_Log("Failed to create Vulkan instance: %d", r);
        return false;
    }

    volkLoadInstance(dev->instance);
    return true;
}

static bool create_debug_messenger(Mel_Gpu_Device* dev)
{
    if (!dev->validation_enabled) return true;

    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };

    VkResult r = vkCreateDebugUtilsMessengerEXT(dev->instance, &create_info, nullptr, &dev->debug_messenger);
    if (r != VK_SUCCESS)
    {
        SDL_Log("Failed to create debug messenger: %d", r);
        return false;
    }
    return true;
}

static bool find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface,
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

        if (surface != VK_NULL_HANDLE)
        {
            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
            if (!found_present && present_support)
            {
                *present = i;
                found_present = true;
            }
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

    if (!found_present)
    {
        *present = *graphics;
    }

    mel_dealloc(mel_alloc_heap(), props);
    return found_graphics;
}

static i32 rate_device(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    if (props.apiVersion < VK_API_VERSION_1_3)
        return -1;

    u32 graphics, present, transfer;
    if (!find_queue_families(device, VK_NULL_HANDLE, &graphics, &present, &transfer))
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

static bool pick_physical_device(Mel_Gpu_Device* dev)
{
    u32 count = 0;
    vkEnumeratePhysicalDevices(dev->instance, &count, nullptr);
    if (count == 0)
    {
        SDL_Log("No Vulkan devices found");
        return false;
    }

    VkPhysicalDevice* devices = mel_alloc(mel_alloc_heap(), sizeof(VkPhysicalDevice) * count);
    vkEnumeratePhysicalDevices(dev->instance, &count, devices);

    log_available_devices(devices, count);

    i32 best_score = -1;
    VkPhysicalDevice best_device = VK_NULL_HANDLE;

    for (u32 i = 0; i < count; i++)
    {
        i32 score = rate_device(devices[i]);
        if (score > best_score)
        {
            best_score = score;
            best_device = devices[i];
        }
    }

    mel_dealloc(mel_alloc_heap(), devices);
    if (best_device == VK_NULL_HANDLE)
    {
        SDL_Log("No suitable Vulkan device found");
        return false;
    }

    dev->physical_device = best_device;

    dev->desc_buffer_props = (VkPhysicalDeviceDescriptorBufferPropertiesEXT){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT,
    };

    dev->device_properties = (VkPhysicalDeviceProperties2){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &dev->desc_buffer_props,
    };

    vkGetPhysicalDeviceProperties2(dev->physical_device, &dev->device_properties);

    SDL_Log("Selected GPU: %s (%s)",
        dev->device_properties.properties.deviceName,
        device_type_name(dev->device_properties.properties.deviceType));
    log_memory_heaps(dev->physical_device);
    log_queue_families(dev->physical_device, VK_NULL_HANDLE);
    return true;
}

static bool create_logical_device(Mel_Gpu_Device* dev)
{
    find_queue_families(dev->physical_device, VK_NULL_HANDLE,
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
    bool has_mesh_shader = has_extension(available_exts, ext_count, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    bool has_portability = has_extension(available_exts, ext_count, "VK_KHR_portability_subset");
    dev->has_portability_subset = has_portability;

    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
    };
    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures bda_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
    };
    VkPhysicalDeviceVulkan11Features vulkan11_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &bda_query,
    };
    VkPhysicalDeviceSynchronization2Features sync2_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = &vulkan11_query,
    };
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &sync2_query,
    };
    void* query_chain = &dynamic_rendering_query;
    descriptor_indexing_query.pNext = query_chain;
    query_chain = &descriptor_indexing_query;
    if (has_mesh_shader)
    {
        mesh_shader_query.pNext = query_chain;
        query_chain = &mesh_shader_query;
    }
    VkPhysicalDeviceFeatures2 supported_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = query_chain,
    };
    vkGetPhysicalDeviceFeatures2(dev->physical_device, &supported_features);

    dev->capabilities = (Mel_Gpu_Capabilities){
        .synchronization2 = sync2_query.synchronization2 == VK_TRUE,
        .dynamic_rendering = dynamic_rendering_query.dynamicRendering == VK_TRUE,
        .buffer_device_address = bda_query.bufferDeviceAddress == VK_TRUE,
        .descriptor_buffer = dev->has_descriptor_buffer,
        .shader_draw_parameters = vulkan11_query.shaderDrawParameters == VK_TRUE,
        .mesh_shader = has_mesh_shader && mesh_shader_query.meshShader == VK_TRUE,
        .descriptor_indexing = descriptor_indexing_query.descriptorBindingPartiallyBound == VK_TRUE
                            && descriptor_indexing_query.runtimeDescriptorArray == VK_TRUE
                            && descriptor_indexing_query.shaderSampledImageArrayNonUniformIndexing == VK_TRUE,
        .multi_draw_indirect = supported_features.features.multiDrawIndirect == VK_TRUE,
        .timestamp_queries = dev->device_properties.properties.limits.timestampPeriod > 0.0f,
        .portability_subset = has_portability,
        .present_queue = dev->has_present_queue,
    };

    mel_dealloc(mel_alloc_heap(), available_exts);

    u32 enabled_ext_count = 0;
    const char* enabled_exts[8];
    enabled_exts[enabled_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (has_portability)
        enabled_exts[enabled_ext_count++] = "VK_KHR_portability_subset";
    if (dev->has_descriptor_buffer)
        enabled_exts[enabled_ext_count++] = VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME;
    if (dev->capabilities.mesh_shader)
        enabled_exts[enabled_ext_count++] = VK_EXT_MESH_SHADER_EXTENSION_NAME;

    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .meshShader = dev->capabilities.mesh_shader ? VK_TRUE : VK_FALSE,
    };

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .descriptorBindingPartiallyBound = dev->capabilities.descriptor_indexing ? VK_TRUE : VK_FALSE,
        .runtimeDescriptorArray = dev->capabilities.descriptor_indexing ? VK_TRUE : VK_FALSE,
        .shaderSampledImageArrayNonUniformIndexing = dev->capabilities.descriptor_indexing ? VK_TRUE : VK_FALSE,
        .descriptorBindingVariableDescriptorCount = dev->capabilities.descriptor_indexing ? VK_TRUE : VK_FALSE,
        .descriptorBindingSampledImageUpdateAfterBind = dev->capabilities.descriptor_indexing ? VK_TRUE : VK_FALSE,
    };

    VkPhysicalDeviceBufferDeviceAddressFeatures bda_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .bufferDeviceAddress = dev->capabilities.buffer_device_address ? VK_TRUE : VK_FALSE,
    };

    VkPhysicalDeviceVulkan11Features vulkan11_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &bda_features,
        .shaderDrawParameters = dev->capabilities.shader_draw_parameters ? VK_TRUE : VK_FALSE,
    };

    VkPhysicalDeviceSynchronization2Features sync2_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = &vulkan11_features,
        .synchronization2 = dev->capabilities.synchronization2 ? VK_TRUE : VK_FALSE,
    };

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &sync2_features,
        .dynamicRendering = dev->capabilities.dynamic_rendering ? VK_TRUE : VK_FALSE,
    };

    descriptor_indexing_features.pNext = &dynamic_rendering_features;
    void* feature_chain = &descriptor_indexing_features;
    if (dev->capabilities.mesh_shader)
    {
        mesh_shader_features.pNext = feature_chain;
        feature_chain = &mesh_shader_features;
    }

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = feature_chain,
        .features = {
            .multiDrawIndirect = dev->capabilities.multi_draw_indirect ? VK_TRUE : VK_FALSE,
        },
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
    if (r != VK_SUCCESS)
    {
        SDL_Log("Failed to create logical device: %d", r);
        return false;
    }

    volkLoadDevice(dev->device);

    vkGetDeviceQueue(dev->device, dev->graphics_family, 0, &dev->graphics_queue);
    vkGetDeviceQueue(dev->device, dev->present_family, 0, &dev->present_queue);
    vkGetDeviceQueue(dev->device, dev->transfer_family, 0, &dev->transfer_queue);
    return true;
}

static bool create_vma(Mel_Gpu_Device* dev)
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
    if (r != VK_SUCCESS)
    {
        SDL_Log("Failed to create VMA allocator: %d", r);
        return false;
    }
    return true;
}

bool mel_gpu_device_init_opt(Mel_Gpu_Device* dev, Mel_Gpu_Device_Opt opt)
{
    assert(dev != nullptr);

    *dev = (Mel_Gpu_Device){0};
    dev->alloc = opt.allocator;

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_func = mel_gpu_vulkan_load();
    if (!vkGetInstanceProcAddr_func)
    {
        SDL_Log("Failed to load Vulkan");
        return false;
    }

    volkInitializeCustom(vkGetInstanceProcAddr_func);

    if (!create_instance(dev, &opt))                                            goto fail;
    if (!create_debug_messenger(dev))                                           goto fail;
    if (!pick_physical_device(dev))                                             goto fail;
    if (!create_logical_device(dev))                                            goto fail;
    if (!create_vma(dev))                                                       goto fail;

    SDL_Log("Vulkan device initialized (Vulkan 1.3, sync2, dynamic rendering, BDA%s%s)",
        dev->has_descriptor_buffer ? ", descriptor buffer" : "",
        dev->capabilities.descriptor_indexing ? ", descriptor indexing" : "");
    return true;

fail:
    mel_gpu_device_shutdown(dev);
    return false;
}

void mel_gpu_device_shutdown(Mel_Gpu_Device* dev)
{
    assert(dev != nullptr);

    if (dev->device) vkDeviceWaitIdle(dev->device);
    if (dev->vma) vmaDestroyAllocator(dev->vma);
    if (dev->device) vkDestroyDevice(dev->device, nullptr);
    if (dev->debug_messenger) vkDestroyDebugUtilsMessengerEXT(dev->instance, dev->debug_messenger, nullptr);
    if (dev->instance) vkDestroyInstance(dev->instance, nullptr);

    SDL_Log("Vulkan device shutdown");
}

void mel_gpu_device_wait_idle(Mel_Gpu_Device* dev)
{
    assert(dev != nullptr);
    vkDeviceWaitIdle(dev->device);
}

Mel_Gpu_Capabilities mel_gpu_capabilities(Mel_Gpu_Device* dev)
{
    assert(dev != nullptr);
    return dev->capabilities;
}

VkSurfaceKHR mel_gpu_surface_create(Mel_Gpu_Device* dev, SDL_Window* window)
{
    assert(dev != nullptr);
    assert(window != nullptr);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, dev->instance, nullptr, &surface))
    {
        SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
        return VK_NULL_HANDLE;
    }

    if (!dev->has_present_queue)
        mel_gpu_device_configure_present(dev, surface);

    return surface;
}

void mel_gpu_surface_destroy(Mel_Gpu_Device* dev, VkSurfaceKHR surface)
{
    assert(dev != nullptr);
    if (surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(dev->instance, surface, nullptr);
}

bool mel_gpu_device_configure_present(Mel_Gpu_Device* dev, VkSurfaceKHR surface)
{
    assert(dev != nullptr);
    assert(surface != VK_NULL_HANDLE);

    u32 graphics, present, transfer;
    if (!find_queue_families(dev->physical_device, surface, &graphics, &present, &transfer))
        return false;

    dev->present_family = present;
    vkGetDeviceQueue(dev->device, dev->present_family, 0, &dev->present_queue);
    dev->has_present_queue = true;
    dev->capabilities.present_queue = true;
    SDL_Log("Present queue configured: graphics=%u present=%u transfer=%u",
        graphics, present, transfer);
    log_queue_families(dev->physical_device, surface);
    return true;
}
