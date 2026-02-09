#ifndef MEL_GPU_SUBMIT_H
#define MEL_GPU_SUBMIT_H

#include "gpu.device.h"

typedef void (*Mel_Gpu_Submit_Fn)(VkCommandBuffer cmd, void* user);

void mel_gpu_submit_immediate(Mel_Gpu_Device* dev, Mel_Gpu_Submit_Fn callback, void* user);

#endif
