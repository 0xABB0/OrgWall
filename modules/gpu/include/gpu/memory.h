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
