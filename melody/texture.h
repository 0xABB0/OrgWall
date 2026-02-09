#ifndef MEL_ASSETS_TEXTURE_H
#define MEL_ASSETS_TEXTURE_H

#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "string.str8.fwd.h"

bool mel_texture_load(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, str8 path);
bool mel_texture_load_and_bind(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, str8 path);

#endif
