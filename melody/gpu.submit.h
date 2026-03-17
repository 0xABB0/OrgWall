#pragma once

#include "gpu.device.fwd.h"
#include "gpu.cmd.fwd.h"
#include "swapchain.fwd.h"

typedef void (*Mel_Gpu_Submit_Fn)(Mel_Gpu_Cmd* cmd, void* user);

void mel_gpu_submit_immediate(Mel_Gpu_Device* dev, Mel_Gpu_Submit_Fn callback, void* user);

typedef struct {
    Mel_Gpu_Submit_Fn callback;
    void* user;
    Mel_Swapchain* swapchain;
} Mel_Gpu_Submit_Frame_Opt;

void mel_gpu_submit_frame_opt(Mel_Gpu_Device* dev, Mel_Gpu_Submit_Frame_Opt opt);
#define mel_gpu_submit_frame(dev, ...) mel_gpu_submit_frame_opt((dev), (Mel_Gpu_Submit_Frame_Opt){__VA_ARGS__})

void mel_gpu_submit_shutdown(Mel_Gpu_Device* dev);
