#pragma once

#include <gpu/types.h>

typedef struct {
    bool debug; // request validation/debug layers where the backend offers them
} Mel_Gpu_Device_Opt;

Mel_Gpu_Device* mel_gpu_device_create_opt(Mel_Gpu_Device_Opt opt);
#define mel_gpu_device_create(...) mel_gpu_device_create_opt((Mel_Gpu_Device_Opt){__VA_ARGS__})

void mel_gpu_device_destroy(Mel_Gpu_Device* dev);
