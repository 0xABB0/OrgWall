#include "vk_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

#include <string.h>

static bool mel_gpu__layer_available(const char* name)
{
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, NULL);
    if (!count)
        return false;
    const Mel_Alloc*   a = mel_alloc_heap();
    VkLayerProperties* layers = mel_alloc_array(a, VkLayerProperties, count);
    vkEnumerateInstanceLayerProperties(&count, layers);
    bool found = false;
    for (u32 i = 0; i < count; i++)
        if (strcmp(layers[i].layerName, name) == 0)
        {
            found = true;
            break;
        }
    mel_dealloc(a, layers);
    return found;
}

static bool mel_gpu__instance_ext_available(const char* name)
{
    u32 count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    if (!count)
        return false;
    const Mel_Alloc*       a = mel_alloc_heap();
    VkExtensionProperties* exts = mel_alloc_array(a, VkExtensionProperties, count);
    vkEnumerateInstanceExtensionProperties(NULL, &count, exts);
    bool found = false;
    for (u32 i = 0; i < count; i++)
        if (strcmp(exts[i].extensionName, name) == 0)
        {
            found = true;
            break;
        }
    mel_dealloc(a, exts);
    return found;
}

Mel_Gpu_Instance* mel_gpu_instance_create_opt(Mel_Gpu_Instance_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    const char* exts[16];
    u32         ext_count = 0;
    exts[ext_count++] = VK_KHR_SURFACE_EXTENSION_NAME;

    bool portability = false;
#if defined(__APPLE__)
    exts[ext_count++] = "VK_EXT_metal_surface";
    if (mel_gpu__instance_ext_available(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        exts[ext_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        portability = true;
    }
    if (mel_gpu__instance_ext_available(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        exts[ext_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
#elif defined(_WIN32)
    if (mel_gpu__instance_ext_available("VK_KHR_win32_surface"))
        exts[ext_count++] = "VK_KHR_win32_surface";
#endif

    bool want_debug = opt.debug.enabled;
    bool have_debug_utils = want_debug && mel_gpu__instance_ext_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    bool have_validation = want_debug && mel_gpu__layer_available("VK_LAYER_KHRONOS_validation");
    if (have_debug_utils)
        exts[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    if (want_debug && !have_validation)
        mel_log_warn("gpu", "validation requested but VK_LAYER_KHRONOS_validation not installed");

    const char* layers[1];
    u32         layer_count = 0;
    if (have_validation)
        layers[layer_count++] = "VK_LAYER_KHRONOS_validation";

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = opt.app_name ? opt.app_name : "melody",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "melody",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_2,
    };

    VkDebugUtilsMessengerCreateInfoEXT dbg = mel_gpu__debug_messenger_info();

    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = exts,
        .enabledLayerCount = layer_count,
        .ppEnabledLayerNames = layer_count ? layers : NULL,
        .pNext = have_debug_utils ? &dbg : NULL,
    };
    if (portability)
        ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    VkInstance vk = VK_NULL_HANDLE;
    VkResult   r = vkCreateInstance(&ci, NULL, &vk);
    if (r != VK_SUCCESS && layer_count > 0)
    {
        mel_log_warn("gpu", "instance creation with validation failed (%s); retrying without the validation layer", mel_gpu__vk_result_str(r));
        ci.enabledLayerCount = 0;
        ci.ppEnabledLayerNames = NULL;
        have_validation = false;
        r = vkCreateInstance(&ci, NULL, &vk);
    }
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkCreateInstance failed: %s", mel_gpu__vk_result_str(r));
        return NULL;
    }

    Mel_Gpu_Instance* inst = mel_alloc_type(alloc, Mel_Gpu_Instance);
    *inst = (Mel_Gpu_Instance){ 0 };
    inst->vk = vk;
    inst->debug = opt.debug;
    inst->alloc = alloc;
    inst->portability = portability;
    inst->messenger = VK_NULL_HANDLE;

    if (have_debug_utils)
        mel_gpu__debug_messenger_create(vk, &inst->messenger);

    u32 phys_count = 0;
    vkEnumeratePhysicalDevices(vk, &phys_count, NULL);
    if (phys_count == 0)
    {
        mel_log_error("gpu", "no Vulkan physical devices found");
        inst->adapters = NULL;
        inst->adapter_count = 0;
        return inst;
    }

    VkPhysicalDevice* physs = mel_alloc_array(alloc, VkPhysicalDevice, phys_count);
    vkEnumeratePhysicalDevices(vk, &phys_count, physs);

    inst->adapters = mel_alloc_array(alloc, Mel_Gpu_Adapter, phys_count);
    inst->adapter_count = phys_count;
    for (u32 i = 0; i < phys_count; i++)
    {
        inst->adapters[i].instance = inst;
        inst->adapters[i].phys = physs[i];
        mel_gpu__caps_probe(physs[i], &inst->adapters[i].caps);
        inst->adapters[i].caps.debug.validation_available = have_validation;
    }
    mel_dealloc(alloc, physs);

    mel_log_info("gpu", "vulkan instance created: %u adapter(s)%s", phys_count, have_validation ? ", validation on" : "");
    return inst;
}

void mel_gpu_instance_destroy(Mel_Gpu_Instance* inst)
{
    if (!inst)
        return;
    if (inst->adapters)
        mel_dealloc(inst->alloc, inst->adapters);
    mel_gpu__debug_messenger_destroy(inst->vk, inst->messenger);
    vkDestroyInstance(inst->vk, NULL);
    mel_dealloc(inst->alloc, inst);
}

u32 mel_gpu_adapters(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter** out, u32 max)
{
    if (!inst)
        return 0;
    u32 n = inst->adapter_count < max ? inst->adapter_count : max;
    for (u32 i = 0; i < n; i++)
        out[i] = &inst->adapters[i];
    return inst->adapter_count;
}

Mel_Gpu_Caps mel_gpu_adapter_caps(Mel_Gpu_Adapter* adapter) { return adapter ? adapter->caps : (Mel_Gpu_Caps){ 0 }; }
