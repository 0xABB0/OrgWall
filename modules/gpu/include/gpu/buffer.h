#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>
#include <gpu/memory.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

MEL_GPU_HANDLE(Mel_Gpu_Buffer);

typedef enum
{
    MEL_GPU_BUFFER_VERTEX = 1u << 0,
    MEL_GPU_BUFFER_INDEX = 1u << 1,
    MEL_GPU_BUFFER_UNIFORM = 1u << 2,
    MEL_GPU_BUFFER_STORAGE = 1u << 3,
    MEL_GPU_BUFFER_TRANSFER_SRC = 1u << 4,
    MEL_GPU_BUFFER_TRANSFER_DST = 1u << 5,
    MEL_GPU_BUFFER_DEVICE_ADDRESS = 1u << 6,
    MEL_GPU_BUFFER_INDIRECT = 1u << 7,
} Mel_Gpu_Buffer_Usage;

typedef enum
{
    MEL_GPU_BUFFER_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_BUFFER_CREATE_OOM = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_BUFFER_CREATE_VK_FAILED = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_BUFFER_CREATE_BAD_PARAMS = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_BUFFER_CREATE_BINDLESS_SLOT_EXHAUSTED = MEL_GPU_STATUS(4, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Buffer_Create_Status;

typedef struct
{
    usize                size;
    Mel_Gpu_Buffer_Usage usage;
    Mel_Gpu_Memory_Role  memory;
    const void*          data;
    const char*          name;
    bool                 capture_replay;
} Mel_Gpu_Buffer_Opt;

typedef struct
{
    Mel_Gpu_Buffer               value;
    Mel_Gpu_Buffer_Create_Status status;
} Mel_Gpu_Buffer_Create_Result;

Mel_Gpu_Buffer_Create_Result mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt);
#define mel_gpu_buffer_create(dev, ...) mel_gpu_buffer_create_opt((dev), (Mel_Gpu_Buffer_Opt){ __VA_ARGS__ })

void  mel_gpu_buffer_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);
bool  mel_gpu_buffer_alive(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);
void  mel_gpu_buffer_write(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, const void* data, usize size);
void* mel_gpu_buffer_mapped(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);

u32 mel_gpu_buffer_make_resident(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);
u32 mel_gpu_buffer_evict(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);

Mel_Gpu_Buffer mel_gpu_buffer_import(Mel_Gpu_Device* dev, void* native_buffer, usize size, const char* name);
