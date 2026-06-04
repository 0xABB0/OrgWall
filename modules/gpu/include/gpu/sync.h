#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

MEL_GPU_HANDLE(Mel_Gpu_Sync);

typedef enum
{
    MEL_GPU_SYNC_BINARY = 0,
    MEL_GPU_SYNC_TIMELINE = 1,
} Mel_Gpu_Sync_Kind;

typedef enum
{
    MEL_GPU_SYNC_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_SYNC_CREATE_BACKEND_FAILED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_SYNC_CREATE_UNSUPPORTED = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Sync_Create_Status;

typedef struct
{
    Mel_Gpu_Sync               value;
    Mel_Gpu_Sync_Create_Status status;
} Mel_Gpu_Sync_Create_Result;

Mel_Gpu_Sync_Create_Result mel_gpu_sync_create(Mel_Gpu_Device* dev, Mel_Gpu_Sync_Kind kind, u64 initial_value);
void                       mel_gpu_sync_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync);
bool                       mel_gpu_sync_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync);
