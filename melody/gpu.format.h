#pragma once

#include "gpu.types.h"

u32 mel_gpu_format_size(Mel_Gpu_Format format);
bool mel_gpu_format_has_depth(Mel_Gpu_Format format);
bool mel_gpu_format_has_stencil(Mel_Gpu_Format format);
bool mel_gpu_format_is_compressed(Mel_Gpu_Format format);
Mel_Gpu_Aspect mel_gpu_format_aspect(Mel_Gpu_Format format);
