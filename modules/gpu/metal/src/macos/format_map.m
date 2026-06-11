#include "mtl_backend.h"

MTLPixelFormat mel_gpu__mtl_format(Mel_Gpu_Format fmt)
{
    switch (fmt)
    {
    case MEL_GPU_FORMAT_BGRA8_UNORM:
        return MTLPixelFormatBGRA8Unorm;
    case MEL_GPU_FORMAT_RGBA8_UNORM:
        return MTLPixelFormatRGBA8Unorm;
    case MEL_GPU_FORMAT_RGBA8_SRGB:
        return MTLPixelFormatRGBA8Unorm_sRGB;
    case MEL_GPU_FORMAT_BGRA8_SRGB:
        return MTLPixelFormatBGRA8Unorm_sRGB;
    case MEL_GPU_FORMAT_RG32_FLOAT:
        return MTLPixelFormatRG32Float;
    case MEL_GPU_FORMAT_RGBA32_FLOAT:
        return MTLPixelFormatRGBA32Float;
    case MEL_GPU_FORMAT_D32_FLOAT:
        return MTLPixelFormatDepth32Float;
    case MEL_GPU_FORMAT_D24_UNORM_S8_UINT:
        return MTLPixelFormatDepth32Float_Stencil8;
    case MEL_GPU_FORMAT_RGB32_FLOAT:
    case MEL_GPU_FORMAT_UNDEFINED:
    default:
        return MTLPixelFormatInvalid;
    }
}

Mel_Gpu_Format mel_gpu__mtl_format_to_mel(MTLPixelFormat fmt)
{
    switch (fmt)
    {
    case MTLPixelFormatBGRA8Unorm:
        return MEL_GPU_FORMAT_BGRA8_UNORM;
    case MTLPixelFormatRGBA8Unorm:
        return MEL_GPU_FORMAT_RGBA8_UNORM;
    case MTLPixelFormatRGBA8Unorm_sRGB:
        return MEL_GPU_FORMAT_RGBA8_SRGB;
    case MTLPixelFormatBGRA8Unorm_sRGB:
        return MEL_GPU_FORMAT_BGRA8_SRGB;
    case MTLPixelFormatRG32Float:
        return MEL_GPU_FORMAT_RG32_FLOAT;
    case MTLPixelFormatRGBA32Float:
        return MEL_GPU_FORMAT_RGBA32_FLOAT;
    case MTLPixelFormatDepth32Float:
        return MEL_GPU_FORMAT_D32_FLOAT;
    case MTLPixelFormatDepth32Float_Stencil8:
        return MEL_GPU_FORMAT_D24_UNORM_S8_UINT;
    default:
        return MEL_GPU_FORMAT_UNDEFINED;
    }
}
