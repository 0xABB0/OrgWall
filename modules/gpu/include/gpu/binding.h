#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

bool mel_gpu_bindless_available(Mel_Gpu_Device* dev);

u32 mel_gpu_texture_view_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view);
u32 mel_gpu_buffer_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);

u64 mel_gpu_buffer_device_address(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);

void mel_gpu_cmd_bind_bindless(Mel_Gpu_Command_List* cmd);
