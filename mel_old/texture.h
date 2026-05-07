#pragma once

#include "gpu.texture.fwd.h"
#include "gpu.pipeline.fwd.h"
#include "gpu.device.fwd.h"
#include "gpu.types.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"

typedef struct {
    u32 format;
    bool nearest_filter;
    bool generate_mips;
    u32 address_mode_u;
    u32 address_mode_v;
    u32 address_mode_w;
} Mel_Texture_Load_Opt;

bool mel_texture_load_opt(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, const Mel_Alloc* alloc, str8 path, Mel_Texture_Load_Opt opt);
#define mel_texture_load(tex, dev, alloc, path, ...) mel_texture_load_opt((tex), (dev), (alloc), (path), (Mel_Texture_Load_Opt){__VA_ARGS__})

bool mel_texture_load_and_bind_opt(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, const Mel_Alloc* alloc, str8 path,
                                   Mel_Texture_Load_Opt opt);
#define mel_texture_load_and_bind(tex, dev, pipeline, alloc, path, ...) \
    mel_texture_load_and_bind_opt((tex), (dev), (pipeline), (alloc), (path), (Mel_Texture_Load_Opt){__VA_ARGS__})
