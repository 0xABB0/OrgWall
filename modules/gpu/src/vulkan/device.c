#include "vk_backend.h"

#include <allocator/heap.h>
#include <log/log.h>
#include <thermal/thermal.h>
#include <power/power.h>

#include <string.h>

static u32 mel_gpu__find_graphics_family(VkPhysicalDevice phys)
{
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, NULL);
    if (!count)
        return UINT32_MAX;
    const Mel_Alloc*         a = mel_alloc_heap();
    VkQueueFamilyProperties* fams = mel_alloc_array(a, VkQueueFamilyProperties, count);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, fams);
    u32 found = UINT32_MAX;
    for (u32 i = 0; i < count; i++)
        if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            found = i;
            break;
        }
    mel_dealloc(a, fams);
    return found;
}

static bool mel_gpu__device_ext_available(VkPhysicalDevice phys, const char* name)
{
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(phys, NULL, &count, NULL);
    if (!count)
        return false;
    const Mel_Alloc*       a = mel_alloc_heap();
    VkExtensionProperties* exts = mel_alloc_array(a, VkExtensionProperties, count);
    vkEnumerateDeviceExtensionProperties(phys, NULL, &count, exts);
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

Mel_Gpu_Device_Create_Result mel_gpu_device_create_opt(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter* adapter, Mel_Gpu_Device_Opt opt)
{
    Mel_Gpu_Device_Create_Result res = { .value = NULL, .status = MEL_GPU_DEVICE_CREATE_OK };

    if (!inst || !adapter)
    {
        res.status = MEL_GPU_DEVICE_CREATE_NO_ADAPTER;
        mel_log_error("gpu", "device_create: null instance or adapter");
        return res;
    }

    u32 gfx = mel_gpu__find_graphics_family(adapter->phys);
    if (gfx == UINT32_MAX)
    {
        res.status = MEL_GPU_DEVICE_CREATE_NO_GRAPHICS_QUEUE;
        mel_log_error("gpu", "device_create: no graphics-capable queue family");
        return res;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    const char* exts[8];
    u32         ext_count = 0;
    if (mel_gpu__device_ext_available(adapter->phys, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        exts[ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (mel_gpu__device_ext_available(adapter->phys, "VK_KHR_portability_subset"))
        exts[ext_count++] = "VK_KHR_portability_subset";
    bool has_budget = mel_gpu__device_ext_available(adapter->phys, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (has_budget)
        exts[ext_count++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
    // U16: dynamic rendering is the engine's primary §7.2 lowering when granted; render passes are the floor.
    bool has_dr = mel_gpu__device_ext_available(adapter->phys, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    if (has_dr)
        exts[ext_count++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;

    float                   prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = gfx,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };

    VkPhysicalDeviceVulkan12Features feat12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    if (opt.features.timeline_semaphores && adapter->caps.queues.timeline == MEL_GPU_TIMELINE_NATIVE)
        feat12.timelineSemaphore = VK_TRUE;
    if (opt.features.buffer_device_address)
        feat12.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR feat_dr = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        .pNext = &feat12,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 feat2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = has_dr ? (void*)&feat_dr : (void*)&feat12,
    };

    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &feat2,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = ext_count ? exts : NULL,
    };

    VkDevice vk = VK_NULL_HANDLE;
    VkResult r = vkCreateDevice(adapter->phys, &dci, NULL, &vk);
    if (r != VK_SUCCESS)
    {
        res.status = r == VK_ERROR_OUT_OF_HOST_MEMORY || r == VK_ERROR_OUT_OF_DEVICE_MEMORY ? MEL_GPU_DEVICE_CREATE_OOM : MEL_GPU_DEVICE_CREATE_VK_FAILED;
        mel_log_error("gpu", "vkCreateDevice failed: %s", mel_gpu__vk_result_str(r));
        return res;
    }

    Mel_Gpu_Device* dev = mel_alloc_type(alloc, Mel_Gpu_Device);
    *dev = (Mel_Gpu_Device){ 0 };
    dev->instance = inst;
    dev->adapter = adapter;
    dev->phys = adapter->phys;
    dev->vk = vk;
    dev->caps = adapter->caps;
    dev->alloc = alloc;
    dev->reactor = opt.reactor;
    dev->debug = opt.debug;
    dev->on_device_lost = opt.on_device_lost;
    dev->device_lost_user = opt.device_lost_user;
    dev->graphics_family = gfx;
    dev->has_memory_budget = has_budget;
    vkGetDeviceQueue(vk, gfx, 0, &dev->graphics_queue);
    vkGetPhysicalDeviceMemoryProperties(adapter->phys, &dev->mem_props);

    if (has_dr)
    {
        dev->cmd_begin_rendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(vk, "vkCmdBeginRenderingKHR");
        dev->cmd_end_rendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(vk, "vkCmdEndRenderingKHR");
        dev->dynamic_rendering = dev->cmd_begin_rendering && dev->cmd_end_rendering;
    }
    mel_log_info("gpu", "render lowering: %s", dev->dynamic_rendering ? "dynamic rendering" : "render passes (floor)");

    dev->caps.power.power_source = (Mel_Gpu_Power_Source)mel_power_source_current();
    Mel_Thermal_Pressure tp = mel_thermal_current();
    dev->caps.power.thermal_pressure = tp > MEL_THERMAL_UNKNOWN ? (Mel_Gpu_Thermal_Tier)(tp - 1) : MEL_GPU_THERMAL_NOMINAL;
    dev->caps.power.low_power_mode = mel_power_low_power_current() == MEL_POWER_LOW_POWER_ON;

    mel_mutex_init(&dev->obj_lock, MEL_MUTEX_PLAIN);
    mel_mutex_init(&dev->submit_lock, MEL_MUTEX_PLAIN);
    mel_mutex_init(&dev->pool_lock, MEL_MUTEX_PLAIN);
    mel_gpu__allocator_init(dev);
    mel_slotmap_init(&dev->buffers.map, alloc, .item_size = sizeof(Mel_Gpu_Buffer_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->textures.map, alloc, .item_size = sizeof(Mel_Gpu_Texture_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->texture_views.map, alloc, .item_size = sizeof(Mel_Gpu_Texture_View_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->shaders.map, alloc, .item_size = sizeof(Mel_Gpu_Shader_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->pipelines.map, alloc, .item_size = sizeof(Mel_Gpu_Pipeline_Obj), .initial_capacity = 16);
    mel_slotmap_init(&dev->syncs.map, alloc, .item_size = sizeof(Mel_Gpu_Sync_Obj), .initial_capacity = 16);
    dev->buffers.init = dev->textures.init = dev->texture_views.init = dev->shaders.init = dev->pipelines.init = dev->syncs.init = true;

    if (opt.debug.thread_safety_tracker)
        dev->tracker = mel_gpu_thread_tracker_create();

    if (opt.reactor)
        dev->pump = mel_gpu_pump_create(opt.reactor);

    res.value = dev;
    mel_log_info("gpu", "device created on '%s'", dev->caps.adapter.name);
    return res;
}

static void mel_gpu__table_report_leaks(Mel_Gpu_Resource_Table* t, const char* kind)
{
    if (!t->init)
        return;
    u32 n = mel_slotmap_count(&t->map);
    if (n == 0)
        return;
    mel_log_error("gpu", "leak: %u live %s resource(s) at device destroy", n, kind);
    u8*   data = mel_slotmap_data(&t->map);
    usize stride = t->map.item_size;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Resource_Header* h = (Mel_Gpu_Resource_Header*)(data + (usize)i * stride);
        mel_log_error("gpu", "  leaked %s '%s'", kind, h->name ? h->name : "(unnamed)");
    }
}

void mel_gpu_device_destroy(Mel_Gpu_Device* dev)
{
    if (!dev)
        return;

    if (dev->vk && !dev->lost)
        vkDeviceWaitIdle(dev->vk);

    // GPU is idle: every submission has retired, so flush all deferred frees before tearing down memory.
    mel_gpu__submit_complete(dev, dev->submit_serial);
    if (dev->deferred)
        mel_dealloc(dev->alloc, dev->deferred);

    mel_gpu__table_report_leaks(&dev->buffers, "buffer");
    mel_gpu__table_report_leaks(&dev->textures, "texture");
    mel_gpu__table_report_leaks(&dev->texture_views, "texture-view");
    mel_gpu__table_report_leaks(&dev->shaders, "shader");
    mel_gpu__table_report_leaks(&dev->pipelines, "pipeline");
    mel_gpu__table_report_leaks(&dev->syncs, "sync");

    for (u32 i = 0; i < dev->thread_pool_count; i++)
        if (dev->thread_pools[i].pool)
            vkDestroyCommandPool(dev->vk, dev->thread_pools[i].pool, NULL);
    if (dev->thread_pools)
        mel_dealloc(dev->alloc, dev->thread_pools);

    mel_slotmap_free(&dev->buffers.map);
    mel_slotmap_free(&dev->textures.map);
    mel_slotmap_free(&dev->texture_views.map);
    mel_slotmap_free(&dev->shaders.map);
    mel_slotmap_free(&dev->pipelines.map);
    mel_slotmap_free(&dev->syncs.map);
    mel_gpu__allocator_shutdown(dev);
    mel_mutex_destroy(&dev->obj_lock);
    mel_mutex_destroy(&dev->submit_lock);
    mel_mutex_destroy(&dev->pool_lock);

    if (dev->pump)
        mel_gpu_pump_destroy(dev->pump);
    if (dev->tracker)
        mel_gpu_thread_tracker_destroy(dev->tracker);

    if (dev->vk)
        vkDestroyDevice(dev->vk, NULL);

    Mel_Gpu_Instance* owned = dev->owns_instance ? dev->instance : NULL;
    const Mel_Alloc*  alloc = dev->alloc;
    mel_dealloc(alloc, dev);
    if (owned)
        mel_gpu_instance_destroy(owned);
}

const Mel_Gpu_Caps* mel_gpu_device_caps(Mel_Gpu_Device* dev) { return dev ? &dev->caps : NULL; }

Mel_Reactor* mel_gpu_device_reactor(Mel_Gpu_Device* dev) { return dev ? dev->reactor : NULL; }

Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj)
{
    mel_mutex_lock(&dev->obj_lock);
    Mel_SlotMap_Handle h = mel_slotmap_insert(&t->map, obj);
    mel_mutex_unlock(&dev->obj_lock);
    return h;
}

void* mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    void* p = mel_slotmap_get(&t->map, h);
    mel_mutex_unlock(&dev->obj_lock);
    return p;
}

bool mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    bool ok = mel_slotmap_remove(&t->map, h);
    mel_mutex_unlock(&dev->obj_lock);
    return ok;
}

static void mel_gpu__free_deferred_entry(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free* e)
{
    if (e->image)
        vkDestroyImage(dev->vk, e->image, NULL);
    if (e->view)
        vkDestroyImageView(dev->vk, e->view, NULL);
    if (e->buffer)
        vkDestroyBuffer(dev->vk, e->buffer, NULL);
    if (e->pipeline)
        vkDestroyPipeline(dev->vk, e->pipeline, NULL);
    if (e->pipeline_layout)
        vkDestroyPipelineLayout(dev->vk, e->pipeline_layout, NULL);
    if (e->shader_vs)
        vkDestroyShaderModule(dev->vk, e->shader_vs, NULL);
    if (e->shader_fs)
        vkDestroyShaderModule(dev->vk, e->shader_fs, NULL);
    if (e->has_alloc)
        mel_gpu__mem_free(dev, &e->alloc);
}

u64 mel_gpu__submit_serial_next(Mel_Gpu_Device* dev)
{
    mel_mutex_lock(&dev->submit_lock);
    u64 s = ++dev->submit_serial;
    mel_mutex_unlock(&dev->submit_lock);
    return s;
}

void mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial)
{
    mel_mutex_lock(&dev->submit_lock);
    if (serial > dev->submit_completed)
        dev->submit_completed = serial;
    u64 wm = dev->submit_completed;
    u32 keep = 0;
    for (u32 i = 0; i < dev->deferred_count; i++)
    {
        if (dev->deferred[i].marker <= wm)
            mel_gpu__free_deferred_entry(dev, &dev->deferred[i]);
        else
            dev->deferred[keep++] = dev->deferred[i];
    }
    dev->deferred_count = keep;
    mel_mutex_unlock(&dev->submit_lock);
}

void mel_gpu__defer_free(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free entry)
{
    mel_mutex_lock(&dev->submit_lock);
    entry.marker = dev->submit_serial;
    if (entry.marker <= dev->submit_completed)
    {
        mel_mutex_unlock(&dev->submit_lock);
        mel_gpu__free_deferred_entry(dev, &entry);
        return;
    }
    if (dev->deferred_count == dev->deferred_cap)
    {
        u32 cap = dev->deferred_cap ? dev->deferred_cap * 2 : 16;
        dev->deferred = dev->deferred ? mel_realloc(dev->alloc, dev->deferred, sizeof(Mel_Gpu_Deferred_Free) * cap) : mel_alloc(dev->alloc, sizeof(Mel_Gpu_Deferred_Free) * cap);
        dev->deferred_cap = cap;
    }
    dev->deferred[dev->deferred_count++] = entry;
    mel_mutex_unlock(&dev->submit_lock);
}

// U15: one command pool per (calling thread, queue family). Recording is lock-free per thread; the registry
// lookup is the only contended point and is rare (once per thread/family). Pools are destroyed at device teardown.
VkCommandPool mel_gpu__thread_pool(Mel_Gpu_Device* dev, u32 family)
{
    Mel_Thread_Id self = mel_thread_current_id();
    mel_mutex_lock(&dev->pool_lock);
    for (u32 i = 0; i < dev->thread_pool_count; i++)
        if (dev->thread_pools[i].family == family && mel_thread_id_equal(dev->thread_pools[i].thread, self))
        {
            VkCommandPool p = dev->thread_pools[i].pool;
            mel_mutex_unlock(&dev->pool_lock);
            return p;
        }

    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = family,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(dev->vk, &pci, NULL, &pool);

    if (dev->thread_pool_count == dev->thread_pool_cap)
    {
        u32 cap = dev->thread_pool_cap ? dev->thread_pool_cap * 2 : 8;
        dev->thread_pools = dev->thread_pools ? mel_realloc(dev->alloc, dev->thread_pools, sizeof(Mel_Gpu_Thread_Pool) * cap) : mel_alloc(dev->alloc, sizeof(Mel_Gpu_Thread_Pool) * cap);
        dev->thread_pool_cap = cap;
    }
    dev->thread_pools[dev->thread_pool_count++] = (Mel_Gpu_Thread_Pool){ .thread = self, .family = family, .pool = pool };
    mel_mutex_unlock(&dev->pool_lock);
    return pool;
}

static Mel_Gpu_Adapter* mel_gpu__pick_adapter(Mel_Gpu_Instance* inst, Mel_Gpu_Power_Preference pref)
{
    Mel_Gpu_Adapter* adapters[16];
    u32              n = mel_gpu_adapters(inst, adapters, 16);
    if (n == 0)
        return NULL;
    if (n > 16)
        n = 16;

    Mel_Gpu_Adapter_Type want = pref == MEL_GPU_POWER_PREFERENCE_LOW ? MEL_GPU_ADAPTER_INTEGRATED : MEL_GPU_ADAPTER_DISCRETE;
    for (u32 i = 0; i < n; i++)
        if (adapters[i]->caps.adapter.adapter_type == want)
            return adapters[i];
    return adapters[0];
}

Mel_Gpu_Future* mel_gpu_device_create_default_opt(Mel_Gpu_Device_Default_Opt opt)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = opt.app_name, .debug = opt.debug);
    Mel_Gpu_Adapter*  adapter = inst ? mel_gpu__pick_adapter(inst, opt.power_preference) : NULL;

    Mel_Gpu_Device_Create_Result dr = { 0 };
    if (adapter)
        dr = mel_gpu_device_create(inst, adapter, .reactor = opt.reactor, .features = opt.features, .debug = opt.debug, .power_preference = opt.power_preference);

    Mel_Gpu_Completion_Pump* pump = dr.value ? dr.value->pump : NULL;
    Mel_Gpu_Future*          f = mel_gpu_future_create(pump, opt.reactor);

    if (dr.value)
    {
        dr.value->owns_instance = true;
        mel_gpu_future_resolve(f, dr.value, dr.status);
    }
    else
    {
        if (inst)
            mel_gpu_instance_destroy(inst);
        mel_gpu_future_resolve(f, NULL, dr.status ? dr.status : MEL_GPU_DEVICE_CREATE_NO_ADAPTER);
    }
    return f;
}
