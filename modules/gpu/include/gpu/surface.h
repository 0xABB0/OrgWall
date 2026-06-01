#pragma once

#include <gpu/types.h>

Mel_Gpu_Surface* mel_gpu_surface_create(Mel_Gpu_Device* dev, void* native);
void             mel_gpu_surface_destroy(Mel_Gpu_Surface* s);

void mel_gpu_surface_reconfigure(Mel_Gpu_Surface* s, i32 width, i32 height);
void mel_gpu_surface_rebuild(Mel_Gpu_Surface* s, void* new_native);
