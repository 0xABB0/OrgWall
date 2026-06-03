#include <image/gpu.h>

typedef struct
{
    const mel_image_format* cpu;
    Mel_Gpu_Format          gpu;
} mel_image_gpu_pair;

static const mel_image_gpu_pair mel_image__gpu_table[] = {
    { &mel_image_rgba8, MEL_GPU_FORMAT_RGBA8_UNORM },
    { &mel_image_rgba8_premul, MEL_GPU_FORMAT_RGBA8_UNORM },
    { &mel_image_rgba8_srgb, MEL_GPU_FORMAT_RGBA8_SRGB },
    { &mel_image_bgra8, MEL_GPU_FORMAT_BGRA8_UNORM },
    { &mel_image_rgba32f, MEL_GPU_FORMAT_RGBA32_FLOAT },
};

static const usize mel_image__gpu_table_count = sizeof(mel_image__gpu_table) / sizeof(mel_image__gpu_table[0]);

Mel_Gpu_Format mel_image_to_gpu_format(const mel_image_format* fmt)
{
    if (!fmt)
        return MEL_GPU_FORMAT_UNDEFINED;

    for (usize i = 0; i < mel_image__gpu_table_count; i++)
        if (mel_image__gpu_table[i].cpu == fmt)
            return mel_image__gpu_table[i].gpu;

    return MEL_GPU_FORMAT_UNDEFINED;
}

const mel_image_format* mel_image_from_gpu_format(Mel_Gpu_Format fmt)
{
    if (fmt == MEL_GPU_FORMAT_UNDEFINED)
        return NULL;

    for (usize i = 0; i < mel_image__gpu_table_count; i++)
        if (mel_image__gpu_table[i].gpu == fmt)
            return mel_image__gpu_table[i].cpu;

    return NULL;
}
