#pragma once

#include "gpu.texture.fwd.h"
#include "gpu.pipeline.fwd.h"
#include "gpu.device.fwd.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"

bool mel_texture_load(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, const Mel_Alloc* alloc, str8 path);
bool mel_texture_load_and_bind(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, const Mel_Alloc* alloc, str8 path);
