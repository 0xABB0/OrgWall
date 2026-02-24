#pragma once

#include "gpu.texture.fwd.h"
#include "gpu.pipeline.fwd.h"
#include "gpu.device.fwd.h"
#include "string.str8.fwd.h"

typedef struct Mel_Vfs Mel_Vfs;
typedef struct Mel_Alloc Mel_Alloc;

bool mel_texture_load(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Vfs* vfs, const Mel_Alloc* alloc, str8 path);
bool mel_texture_load_and_bind(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, Mel_Vfs* vfs, const Mel_Alloc* alloc, str8 path);
