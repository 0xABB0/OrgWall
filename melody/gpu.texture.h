#ifndef MEL_GPU_TEXTURE_H
#define MEL_GPU_TEXTURE_H

#include "gpu.image.h"
#include "string.str8.fwd.h"

typedef struct Mel_Gpu_Texture Mel_Gpu_Texture;

struct Mel_Gpu_Texture {
    Mel_Gpu_Image image;
    VkSampler sampler;
    VkDescriptorSet descriptor;
};

typedef struct {
    str8 path;
    const u8* data;
    u32 data_size;
    bool nearest_filter;
    const Mel_Alloc* alloc;
} Mel_Gpu_Texture_Opt;

void mel_gpu_texture_init_opt(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Texture_Opt opt);
#define mel_gpu_texture_init(tex, dev, ...) mel_gpu_texture_init_opt((tex), (dev), (Mel_Gpu_Texture_Opt){__VA_ARGS__})

void mel_gpu_texture_init_white(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev);

void mel_gpu_texture_shutdown(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev);

#endif
