#include "vk_backend.h"

#include <allocator/heap.h>

#include <stdio.h>
#include <string.h>

static bool mel_gpu__phys_ext(VkPhysicalDevice phys, const char* name)
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

static Mel_Gpu_Adapter_Type mel_gpu__adapter_type(VkPhysicalDeviceType t)
{
    switch (t)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return MEL_GPU_ADAPTER_DISCRETE;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return MEL_GPU_ADAPTER_INTEGRATED;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return MEL_GPU_ADAPTER_VIRTUAL;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return MEL_GPU_ADAPTER_SOFTWARE;
    default:
        return MEL_GPU_ADAPTER_EXTERNAL;
    }
}

void mel_gpu__caps_probe(Mel_Gpu_Instance* inst, VkPhysicalDevice phys, Mel_Gpu_Caps* out)
{
    *out = (Mel_Gpu_Caps){ 0 };

    VkPhysicalDeviceVulkan11Properties props11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES };
    VkPhysicalDeviceSubgroupProperties subgroup = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, .pNext = &props11 };
    VkPhysicalDeviceProperties2        props2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &subgroup };
    inst->get_physical_device_properties2(phys, &props2);

    VkPhysicalDeviceProperties* p = &props2.properties;

    out->adapter.adapter_type = mel_gpu__adapter_type(p->deviceType);
    out->adapter.vendor_id = p->vendorID;
    out->adapter.device_id = p->deviceID;
    out->adapter.driver_version = p->driverVersion;
    memcpy(out->adapter.uuid, props11.deviceUUID, 16);
    out->adapter.has_luid = props11.deviceLUIDValid;
    if (props11.deviceLUIDValid)
        memcpy(&out->adapter.luid, props11.deviceLUID, sizeof(u64) < VK_LUID_SIZE ? sizeof(u64) : VK_LUID_SIZE);
    snprintf(out->adapter.name, sizeof(out->adapter.name), "%s", p->deviceName);

    VkPhysicalDeviceShaderDrawParametersFeatures sdp = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES };
    VkPhysicalDeviceDescriptorIndexingFeatures di = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, .pNext = &sdp };
    VkPhysicalDeviceVulkan12Features feat12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &di };
    VkPhysicalDeviceFeatures2        feat2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &feat12 };
    inst->get_physical_device_features2(phys, &feat2);

    out->shader.int16 = feat2.features.shaderInt16;
    out->shader.int64 = feat2.features.shaderInt64;
    out->shader.fp64 = feat2.features.shaderFloat64;
    out->shader.fp16 = feat12.shaderFloat16;
    out->shader.int8 = feat12.shaderInt8;
    out->shader.wave_ops = (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) != 0;
    out->shader.draw_parameters = sdp.shaderDrawParameters != 0;
    out->shader.subgroup_size_min = subgroup.subgroupSize;
    out->shader.subgroup_size_max = subgroup.subgroupSize;
    out->shader.bytecode_passthrough.spirv = true;

    out->queues.timeline = feat12.timelineSemaphore ? MEL_GPU_TIMELINE_NATIVE : MEL_GPU_TIMELINE_EMULATED;
    out->queues.internally_synchronized_queues = MEL_GPU_INTERNAL_SYNC_NONE;

    u32 fam_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &fam_count, NULL);
    if (fam_count)
    {
        const Mel_Alloc*         fa = mel_alloc_heap();
        VkQueueFamilyProperties* fams = mel_alloc_array(fa, VkQueueFamilyProperties, fam_count);
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &fam_count, fams);
        for (u32 i = 0; i < fam_count; i++)
        {
            VkQueueFlags f = fams[i].queueFlags;
            bool         gfx = (f & VK_QUEUE_GRAPHICS_BIT) != 0;
            bool         cmp = (f & VK_QUEUE_COMPUTE_BIT) != 0;
            bool         xfr = (f & VK_QUEUE_TRANSFER_BIT) != 0;
            if (cmp && !gfx)
            {
                out->queues.async_compute = true;
                out->queues.dedicated_compute = true;
            }
            if (xfr && !gfx && !cmp)
                out->queues.dedicated_transfer = true;
        }
        mel_dealloc(fa, fams);
    }

    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);

    bool host_visible_device_local = false;
    bool host_visible_any = false;
    for (u32 i = 0; i < mem.memoryTypeCount; i++)
    {
        VkMemoryPropertyFlags f = mem.memoryTypes[i].propertyFlags;
        if (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            host_visible_any = true;
        if ((f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            host_visible_device_local = true;
    }
    for (u32 i = 0; i < mem.memoryHeapCount; i++)
    {
        if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            out->memory.device_local_bytes += mem.memoryHeaps[i].size;
        else
            out->memory.host_visible_bytes += mem.memoryHeaps[i].size;
    }

    bool integrated = out->adapter.adapter_type == MEL_GPU_ADAPTER_INTEGRATED;
    if (host_visible_device_local)
        out->memory.host_visible_device_local = integrated ? MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_FULL_UMA : MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_REBAR;
    else
        out->memory.host_visible_device_local = MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_NONE;

    out->memory.persistent_map = host_visible_any;
    out->memory.residency_control = mel_gpu__phys_ext(phys, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) ? MEL_GPU_RESIDENCY_BUDGET_ONLY : MEL_GPU_RESIDENCY_NONE;
    out->memory.sparse_buffer = feat2.features.sparseBinding && feat2.features.sparseResidencyBuffer;
    out->memory.sparse_texture = feat2.features.sparseBinding && feat2.features.sparseResidencyImage2D;

    bool di_full = di.runtimeDescriptorArray && di.descriptorBindingPartiallyBound && di.shaderSampledImageArrayNonUniformIndexing && di.descriptorBindingSampledImageUpdateAfterBind && di.descriptorBindingStorageBufferUpdateAfterBind && di.descriptorBindingUniformBufferUpdateAfterBind && di.descriptorBindingStorageImageUpdateAfterBind;
    out->memory.bindless.tier = di_full ? MEL_GPU_TIER_FULL : MEL_GPU_TIER_NONE;
    if (di_full)
    {
        VkPhysicalDeviceDescriptorIndexingProperties dip = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES };
        VkPhysicalDeviceProperties2 dprops = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &dip };
        inst->get_physical_device_properties2(phys, &dprops);
        u32 share = dip.maxPerStageUpdateAfterBindResources / 4;
        out->memory.bindless.max_texture_view_slots = dip.maxPerStageDescriptorUpdateAfterBindSampledImages < share ? dip.maxPerStageDescriptorUpdateAfterBindSampledImages : share;
        out->memory.bindless.max_sampler_slots = dip.maxPerStageDescriptorUpdateAfterBindSamplers;
        out->memory.bindless.max_storage_buffer_slots = dip.maxPerStageDescriptorUpdateAfterBindStorageBuffers < share ? dip.maxPerStageDescriptorUpdateAfterBindStorageBuffers : share;
        out->memory.bindless.max_uniform_buffer_slots = dip.maxPerStageDescriptorUpdateAfterBindUniformBuffers < share ? dip.maxPerStageDescriptorUpdateAfterBindUniformBuffers : share;
        out->memory.bindless.max_storage_image_slots = dip.maxPerStageDescriptorUpdateAfterBindStorageImages < share ? dip.maxPerStageDescriptorUpdateAfterBindStorageImages : share;
        out->memory.bindless.growable = di.descriptorBindingVariableDescriptorCount;
    }

    out->features.ray_tracing = MEL_GPU_RT_NONE;
    out->features.video_decode = MEL_GPU_VIDEO_NONE;
    out->features.video_encode = MEL_GPU_VIDEO_NONE;
    out->features.video_process = MEL_GPU_VIDEO_NONE;
    out->features.asset_io = MEL_GPU_ASSET_IO_CPU_STAGED;
    out->features.mesh_shaders = false;
    out->features.work_graphs = false;
    out->features.ml_tensor = false;

    out->presentation.allow_tearing = false;
    out->presentation.vrr = false;
    out->presentation.frame_latency_waitable = false;
    out->presentation.shared_presentable_image = mel_gpu__phys_ext(phys, VK_KHR_SHARED_PRESENTABLE_IMAGE_EXTENSION_NAME);
    out->presentation.pre_rotation = false;
    out->presentation.present_wait = MEL_GPU_PRESENT_WAIT_NONE;
    out->presentation.present_timing_feedback = MEL_GPU_PRESENT_TIMING_NONE;

    out->queries.timestamp = p->limits.timestampPeriod > 0.0f ? MEL_GPU_TIMESTAMP_NATIVE : MEL_GPU_TIMESTAMP_NONE;
    out->queries.timestamp_period_ns = (f64)p->limits.timestampPeriod;
    out->queries.timestamp_compute_and_graphics = p->limits.timestampComputeAndGraphics != 0;
    out->queries.timestamp_calibrated = MEL_GPU_TIMESTAMP_CALIBRATED_NONE;
    out->queries.occlusion_precise = feat2.features.occlusionQueryPrecise;
    out->queries.pipeline_statistics = feat2.features.pipelineStatisticsQuery;

    out->power.power_source = MEL_GPU_POWER_SOURCE_UNKNOWN;
    out->power.thermal_pressure = MEL_GPU_THERMAL_NOMINAL;
    out->power.low_power_mode = false;

    out->debug.capture_replay = MEL_GPU_CAPTURE_REPLAY_NONE;

    out->raster.tile_local = integrated ? MEL_GPU_TILE_LOCAL_EMULATED : MEL_GPU_TILE_LOCAL_NONE;
    out->raster.fill_mode_non_solid = feat2.features.fillModeNonSolid != 0;
}
