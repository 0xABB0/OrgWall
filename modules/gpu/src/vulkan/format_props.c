#include "vk_backend.h"

#include <gpu/format_props.h>

static u32 mel_gpu__map_format_features(VkFormatFeatureFlags f)
{
    u32 out = 0;
    if (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        out |= MEL_GPU_FMT_SAMPLED;
    if (f & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
        out |= MEL_GPU_FMT_STORAGE;
    if (f & VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT)
        out |= MEL_GPU_FMT_STORAGE_ATOMIC;
    if (f & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        out |= MEL_GPU_FMT_COLOR_ATTACHMENT;
    if (f & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        out |= MEL_GPU_FMT_COLOR_BLEND;
    if (f & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        out |= MEL_GPU_FMT_DEPTH_ATTACHMENT;
    if (f & VK_FORMAT_FEATURE_BLIT_SRC_BIT)
        out |= MEL_GPU_FMT_BLIT_SRC;
    if (f & VK_FORMAT_FEATURE_BLIT_DST_BIT)
        out |= MEL_GPU_FMT_BLIT_DST;
    if (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        out |= MEL_GPU_FMT_LINEAR_FILTER;
    if (f & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)
        out |= MEL_GPU_FMT_TRANSFER_SRC;
    if (f & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
        out |= MEL_GPU_FMT_TRANSFER_DST;
    return out;
}

Mel_Gpu_Format_Properties mel_gpu_format_properties(Mel_Gpu_Device* dev, Mel_Gpu_Format format, Mel_Gpu_Tiling tiling)
{
    Mel_Gpu_Format_Properties out = { 0 };
    if (!dev)
        return out;

    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(dev->phys, mel_gpu__vk_format(format), &fp);

    VkFormatFeatureFlags primary = tiling == MEL_GPU_TILING_LINEAR ? fp.linearTilingFeatures : fp.optimalTilingFeatures;
    out.tiling_features = mel_gpu__map_format_features(primary);
    out.linear_tiling_features = mel_gpu__map_format_features(fp.linearTilingFeatures);
    out.buffer_features = (fp.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) ? MEL_GPU_FMT_VERTEX_BUFFER : 0;
    out.sample_counts = 0;
    return out;
}
