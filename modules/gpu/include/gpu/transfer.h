#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>
#include <gpu/future.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;
typedef struct Mel_Gpu_Queue  Mel_Gpu_Queue;

typedef enum
{
    MEL_GPU_TRANSFER_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_TRANSFER_BAD_PARAMS = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_TRANSFER_BACKEND_FAILED = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Transfer_Status;

Mel_Gpu_Future* mel_gpu_buffer_upload_async(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Buffer dst, usize dst_offset, const void* data, usize size);

Mel_Gpu_Future* mel_gpu_texture_upload_async(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Texture dst, Mel_Gpu_Texture_Region region, const void* data, usize size);
