#pragma once

#include "gpu.image.h"
#include "string.str8.fwd.h"

typedef struct Mel_Gpu_Texture Mel_Gpu_Texture;

#define MEL_GPU_SAMPLER_ADDRESS_DEFAULT         0u
#define MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE   1u
#define MEL_GPU_SAMPLER_ADDRESS_REPEAT          2u
#define MEL_GPU_SAMPLER_ADDRESS_MIRRORED_REPEAT 3u

struct Mel_Gpu_Texture {
    Mel_Gpu_Image image;
    void* _sampler;
    void* _descriptor;
};

typedef struct {
    bool nearest_filter;
    u32 address_mode_u;
    u32 address_mode_v;
    u32 address_mode_w;
    bool compare_enable;
    Mel_Gpu_Compare_Op compare_op;
    f32 min_lod;
    f32 max_lod;
} Mel_Gpu_Sampler_Opt;

typedef struct {
    str8 path;
    const u8* data;
    u32 data_size;
    const u8* pixels;
    u32 width;
    u32 height;
    Mel_Gpu_Format format;
    bool nearest_filter;
    bool generate_mips;
    u32 address_mode_u;
    u32 address_mode_v;
    u32 address_mode_w;
    const Mel_Alloc* alloc;
} Mel_Gpu_Texture_Opt;

void mel_gpu_texture_init_opt(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Texture_Opt opt);
#define mel_gpu_texture_init(tex, dev, ...) mel_gpu_texture_init_opt((tex), (dev), (Mel_Gpu_Texture_Opt){__VA_ARGS__})

void mel_gpu_texture_init_white(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev);

void mel_gpu_texture_shutdown(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev);

void* mel_gpu_sampler_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt);
#define mel_gpu_sampler_create(dev, ...) mel_gpu_sampler_create_opt((dev), (Mel_Gpu_Sampler_Opt){__VA_ARGS__})
void mel_gpu_sampler_destroy(Mel_Gpu_Device* dev, void* sampler);
