#pragma once

#include "gpu.device.fwd.h"
#include "gpu.cmd.fwd.h"

typedef void (*Mel_Gpu_Submit_Fn)(Mel_Gpu_Cmd* cmd, void* user);

void mel_gpu_submit_immediate(Mel_Gpu_Device* dev, Mel_Gpu_Submit_Fn callback, void* user);
void mel_gpu_submit_shutdown(Mel_Gpu_Device* dev);
