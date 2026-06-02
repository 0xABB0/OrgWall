#pragma once

#include <gpu.h>

typedef struct
{
    Mel_Gpu_Device*  dev;
    Mel_Gpu_Shader   shader;
    Mel_Gpu_Pipeline pipeline;
    Mel_Gpu_Sampler  sampler;
} Bindless_Present;

bool bindless_present_init(Bindless_Present* bp, Mel_Gpu_Device* dev, Mel_Gpu_Format color_format);

void bindless_present_blit(Bindless_Present* bp, Mel_Gpu_Command_List* cmd, u32 tex_slot, Mel_Gpu_Color clear);

void bindless_present_teardown(Bindless_Present* bp);
