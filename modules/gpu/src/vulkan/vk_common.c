#include "vk_backend.h"

#include <log/log.h>

const char* mel_gpu__vk_result_str(VkResult r)
{
    switch (r)
    {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    default:
        return "VK_ERROR_UNKNOWN";
    }
}

VkFormat mel_gpu__vk_format(Mel_Gpu_Format fmt)
{
    switch (fmt)
    {
    case MEL_GPU_FORMAT_BGRA8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case MEL_GPU_FORMAT_RGBA8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case MEL_GPU_FORMAT_RGBA8_SRGB:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case MEL_GPU_FORMAT_BGRA8_SRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case MEL_GPU_FORMAT_RG32_FLOAT:
        return VK_FORMAT_R32G32_SFLOAT;
    case MEL_GPU_FORMAT_RGB32_FLOAT:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case MEL_GPU_FORMAT_RGBA32_FLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case MEL_GPU_FORMAT_D32_FLOAT:
        return VK_FORMAT_D32_SFLOAT;
    case MEL_GPU_FORMAT_D24_UNORM_S8_UINT:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case MEL_GPU_FORMAT_UNDEFINED:
        return VK_FORMAT_UNDEFINED;
    }
    return VK_FORMAT_UNDEFINED;
}

// Shared by the U11 sampler compare and the U13 depth/stencil compare (gpu-rhi.md §6.3 / §6.5). NONE — only
// meaningful for a non-shadow sampler — maps to NEVER; the depth path rejects NONE-while-testing before calling.
VkCompareOp mel_gpu__vk_compare_op(Mel_Gpu_Compare_Op c)
{
    switch (c)
    {
    case MEL_GPU_COMPARE_NONE:
    case MEL_GPU_COMPARE_NEVER:
        return VK_COMPARE_OP_NEVER;
    case MEL_GPU_COMPARE_LESS:
        return VK_COMPARE_OP_LESS;
    case MEL_GPU_COMPARE_EQUAL:
        return VK_COMPARE_OP_EQUAL;
    case MEL_GPU_COMPARE_LESS_EQUAL:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case MEL_GPU_COMPARE_GREATER:
        return VK_COMPARE_OP_GREATER;
    case MEL_GPU_COMPARE_NOT_EQUAL:
        return VK_COMPARE_OP_NOT_EQUAL;
    case MEL_GPU_COMPARE_GREATER_EQUAL:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case MEL_GPU_COMPARE_ALWAYS:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

Mel_Gpu_Format mel_gpu__vk_format_to_mel(VkFormat fmt)
{
    switch (fmt)
    {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return MEL_GPU_FORMAT_BGRA8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return MEL_GPU_FORMAT_RGBA8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return MEL_GPU_FORMAT_RGBA8_SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return MEL_GPU_FORMAT_BGRA8_SRGB;
    case VK_FORMAT_R32G32_SFLOAT:
        return MEL_GPU_FORMAT_RG32_FLOAT;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return MEL_GPU_FORMAT_RGB32_FLOAT;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return MEL_GPU_FORMAT_RGBA32_FLOAT;
    case VK_FORMAT_D32_SFLOAT:
        return MEL_GPU_FORMAT_D32_FLOAT;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return MEL_GPU_FORMAT_D24_UNORM_S8_UINT;
    default:
        return MEL_GPU_FORMAT_UNDEFINED;
    }
}

u32 mel_gpu__vk_find_memory_type(Mel_Gpu_Device* dev, u32 type_bits, VkMemoryPropertyFlags props)
{
    for (u32 i = 0; i < dev->mem_props.memoryTypeCount; i++)
    {
        if ((type_bits & (1u << i)) && (dev->mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

bool mel_gpu__device_is_lost(Mel_Gpu_Device* dev, VkResult r, const char* where)
{
    if (r != VK_ERROR_DEVICE_LOST)
        return false;

    if (!dev->lost)
    {
        dev->lost = true;
        mel_log_error("gpu", "device lost at %s", where);
        if (dev->on_device_lost)
            dev->on_device_lost(dev, where, dev->device_lost_user);
    }
    return true;
}
