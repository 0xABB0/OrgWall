#pragma once

#include <core/types.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

typedef enum
{
    MEL_GPU_MEMORY_DEVICE = 0,
    MEL_GPU_MEMORY_UPLOAD = 1,
    MEL_GPU_MEMORY_READBACK = 2,
} Mel_Gpu_Memory_Role;

typedef struct
{
    u64 budget_bytes;
    u64 usage_bytes;
} Mel_Gpu_Memory_Budget;

Mel_Gpu_Memory_Budget mel_gpu_memory_budget(Mel_Gpu_Device* dev);

typedef void (*Mel_Gpu_Budget_Pressure_Fn)(Mel_Gpu_Device* dev, Mel_Gpu_Memory_Budget budget, void* user);

void mel_gpu_set_budget_pressure_callback(Mel_Gpu_Device* dev, Mel_Gpu_Budget_Pressure_Fn cb, void* user);
