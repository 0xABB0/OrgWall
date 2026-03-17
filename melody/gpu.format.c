#include "gpu.format.h"

u32 mel_gpu_format_size(Mel_Gpu_Format format)
{
    switch (format)
    {
        case MEL_GPU_FORMAT_R8_UNORM:
        case MEL_GPU_FORMAT_R8_SNORM:
        case MEL_GPU_FORMAT_R8_UINT:
        case MEL_GPU_FORMAT_R8_SINT:
        case MEL_GPU_FORMAT_S8_UINT:
            return 1;

        case MEL_GPU_FORMAT_R8G8_UNORM:
        case MEL_GPU_FORMAT_R8G8_SNORM:
        case MEL_GPU_FORMAT_R8G8_UINT:
        case MEL_GPU_FORMAT_R8G8_SINT:
        case MEL_GPU_FORMAT_R16_UNORM:
        case MEL_GPU_FORMAT_R16_SNORM:
        case MEL_GPU_FORMAT_R16_UINT:
        case MEL_GPU_FORMAT_R16_SINT:
        case MEL_GPU_FORMAT_R16_SFLOAT:
        case MEL_GPU_FORMAT_D16_UNORM:
            return 2;

        case MEL_GPU_FORMAT_R8G8B8_UNORM:
        case MEL_GPU_FORMAT_R8G8B8_SNORM:
        case MEL_GPU_FORMAT_R8G8B8_UINT:
        case MEL_GPU_FORMAT_R8G8B8_SINT:
        case MEL_GPU_FORMAT_B8G8R8_UNORM:
        case MEL_GPU_FORMAT_D16_UNORM_S8_UINT:
            return 3;

        case MEL_GPU_FORMAT_R8G8B8A8_UNORM:
        case MEL_GPU_FORMAT_R8G8B8A8_SNORM:
        case MEL_GPU_FORMAT_R8G8B8A8_UINT:
        case MEL_GPU_FORMAT_R8G8B8A8_SINT:
        case MEL_GPU_FORMAT_R8G8B8A8_SRGB:
        case MEL_GPU_FORMAT_B8G8R8A8_UNORM:
        case MEL_GPU_FORMAT_B8G8R8A8_SRGB:
        case MEL_GPU_FORMAT_A2B10G10R10_UNORM_PACK32:
        case MEL_GPU_FORMAT_R16G16_UNORM:
        case MEL_GPU_FORMAT_R16G16_SNORM:
        case MEL_GPU_FORMAT_R16G16_UINT:
        case MEL_GPU_FORMAT_R16G16_SINT:
        case MEL_GPU_FORMAT_R16G16_SFLOAT:
        case MEL_GPU_FORMAT_R32_UINT:
        case MEL_GPU_FORMAT_R32_SINT:
        case MEL_GPU_FORMAT_R32_SFLOAT:
        case MEL_GPU_FORMAT_D32_SFLOAT:
        case MEL_GPU_FORMAT_D24_UNORM_S8_UINT:
        case MEL_GPU_FORMAT_X8_D24_UNORM_PACK32:
            return 4;

        case MEL_GPU_FORMAT_D32_SFLOAT_S8_UINT:
            return 5;

        case MEL_GPU_FORMAT_R16G16B16_UNORM:
        case MEL_GPU_FORMAT_R16G16B16_SFLOAT:
            return 6;

        case MEL_GPU_FORMAT_R16G16B16A16_UNORM:
        case MEL_GPU_FORMAT_R16G16B16A16_SNORM:
        case MEL_GPU_FORMAT_R16G16B16A16_UINT:
        case MEL_GPU_FORMAT_R16G16B16A16_SINT:
        case MEL_GPU_FORMAT_R16G16B16A16_SFLOAT:
        case MEL_GPU_FORMAT_R32G32_UINT:
        case MEL_GPU_FORMAT_R32G32_SINT:
        case MEL_GPU_FORMAT_R32G32_SFLOAT:
        case MEL_GPU_FORMAT_R64_SFLOAT:
            return 8;

        case MEL_GPU_FORMAT_R32G32B32_UINT:
        case MEL_GPU_FORMAT_R32G32B32_SINT:
        case MEL_GPU_FORMAT_R32G32B32_SFLOAT:
            return 12;

        case MEL_GPU_FORMAT_R32G32B32A32_UINT:
        case MEL_GPU_FORMAT_R32G32B32A32_SINT:
        case MEL_GPU_FORMAT_R32G32B32A32_SFLOAT:
        case MEL_GPU_FORMAT_R64G64_SFLOAT:
            return 16;

        default:
            return 0;
    }
}

bool mel_gpu_format_has_depth(Mel_Gpu_Format format)
{
    switch (format)
    {
        case MEL_GPU_FORMAT_D16_UNORM:
        case MEL_GPU_FORMAT_D16_UNORM_S8_UINT:
        case MEL_GPU_FORMAT_D24_UNORM_S8_UINT:
        case MEL_GPU_FORMAT_X8_D24_UNORM_PACK32:
        case MEL_GPU_FORMAT_D32_SFLOAT:
        case MEL_GPU_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

bool mel_gpu_format_has_stencil(Mel_Gpu_Format format)
{
    switch (format)
    {
        case MEL_GPU_FORMAT_S8_UINT:
        case MEL_GPU_FORMAT_D16_UNORM_S8_UINT:
        case MEL_GPU_FORMAT_D24_UNORM_S8_UINT:
        case MEL_GPU_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

bool mel_gpu_format_is_compressed(Mel_Gpu_Format format)
{
    switch (format)
    {
        case MEL_GPU_FORMAT_BC1_RGB_UNORM_BLOCK:
        case MEL_GPU_FORMAT_BC1_RGB_SRGB_BLOCK:
        case MEL_GPU_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case MEL_GPU_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case MEL_GPU_FORMAT_BC2_UNORM_BLOCK:
        case MEL_GPU_FORMAT_BC2_SRGB_BLOCK:
        case MEL_GPU_FORMAT_BC3_UNORM_BLOCK:
        case MEL_GPU_FORMAT_BC3_SRGB_BLOCK:
        case MEL_GPU_FORMAT_BC4_UNORM_BLOCK:
        case MEL_GPU_FORMAT_BC4_SNORM_BLOCK:
        case MEL_GPU_FORMAT_BC5_UNORM_BLOCK:
        case MEL_GPU_FORMAT_BC5_SNORM_BLOCK:
        case MEL_GPU_FORMAT_BC6H_UFLOAT_BLOCK:
        case MEL_GPU_FORMAT_BC6H_SFLOAT_BLOCK:
        case MEL_GPU_FORMAT_BC7_UNORM_BLOCK:
        case MEL_GPU_FORMAT_BC7_SRGB_BLOCK:
        case MEL_GPU_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        case MEL_GPU_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
        case MEL_GPU_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        case MEL_GPU_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
        case MEL_GPU_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        case MEL_GPU_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
        case MEL_GPU_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case MEL_GPU_FORMAT_ASTC_4x4_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

Mel_Gpu_Aspect mel_gpu_format_aspect(Mel_Gpu_Format format)
{
    bool depth = mel_gpu_format_has_depth(format);
    bool stencil = mel_gpu_format_has_stencil(format);

    if (depth && stencil)
        return MEL_GPU_ASPECT_DEPTH | MEL_GPU_ASPECT_STENCIL;
    if (depth)
        return MEL_GPU_ASPECT_DEPTH;
    if (stencil)
        return MEL_GPU_ASPECT_STENCIL;
    return MEL_GPU_ASPECT_COLOR;
}
