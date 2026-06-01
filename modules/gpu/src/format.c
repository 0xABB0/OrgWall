#include <gpu/format.h>

u32 mel_gpu_format_bytes(Mel_Gpu_Format format)
{
    switch (format)
    {
    case MEL_GPU_FORMAT_BGRA8_UNORM:
    case MEL_GPU_FORMAT_RGBA8_UNORM:
    case MEL_GPU_FORMAT_RGBA8_SRGB:
    case MEL_GPU_FORMAT_BGRA8_SRGB:
    case MEL_GPU_FORMAT_D32_FLOAT:
    case MEL_GPU_FORMAT_D24_UNORM_S8_UINT:
        return 4;
    case MEL_GPU_FORMAT_RG32_FLOAT:
        return 8;
    case MEL_GPU_FORMAT_RGB32_FLOAT:
        return 12;
    case MEL_GPU_FORMAT_RGBA32_FLOAT:
        return 16;
    case MEL_GPU_FORMAT_UNDEFINED:
        return 0;
    }
    return 0;
}

bool mel_gpu_format_is_depth(Mel_Gpu_Format format) { return format == MEL_GPU_FORMAT_D32_FLOAT || format == MEL_GPU_FORMAT_D24_UNORM_S8_UINT; }
