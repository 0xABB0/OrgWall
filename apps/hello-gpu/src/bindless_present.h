#pragma once

#include <gpu.h>

// Shared "present an offscreen/heap texture to the swapchain" helper. Several
// screens render into their own color texture (offscreen depth pass, compute
// result, post-process source) and then need a fullscreen pass that samples that
// texture through the bindless heap. This bundles the blit pipeline + sampler so
// each screen records the present in two lines (MEL-ENGINE-IX: composed, not copied).
typedef struct
{
    Mel_Gpu_Device*  dev;
    Mel_Gpu_Shader   shader;
    Mel_Gpu_Pipeline pipeline;
    Mel_Gpu_Sampler  sampler;
} Bindless_Present;

// Builds the blit pipeline for the given swapchain color format. Returns false if
// shader/pipeline creation failed.
bool bindless_present_init(Bindless_Present* bp, Mel_Gpu_Device* dev, Mel_Gpu_Format color_format);

// Records a fullscreen pass into the swapchain that samples the texture view at
// `tex_slot` through the heap. Must be called between frame_begin and frame_end.
void bindless_present_blit(Bindless_Present* bp, Mel_Gpu_Command_List* cmd, u32 tex_slot, Mel_Gpu_Color clear);

void bindless_present_teardown(Bindless_Present* bp);
