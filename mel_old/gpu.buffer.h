#pragma once

#include "gpu.types.h"
#include "gpu.device.fwd.h"

typedef struct Mel_Gpu_Buffer Mel_Gpu_Buffer;

struct Mel_Gpu_Buffer {
    void* _handle;
    void* _allocation;
    u64 size;
    void* mapped;
    u64 device_address;
    Mel_Gpu_Buffer_Usage usage;
    Mel_Gpu_Stage current_stage;
    Mel_Gpu_Access current_access;
};

typedef struct {
    u64 size;
    Mel_Gpu_Buffer_Usage usage;
    Mel_Gpu_Memory_Usage memory_usage;
    bool map_on_create;
} Mel_Gpu_Buffer_Opt;

void mel_gpu_buffer_init_opt(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt);
#define mel_gpu_buffer_init(buf, dev, ...) mel_gpu_buffer_init_opt((buf), (dev), (Mel_Gpu_Buffer_Opt){__VA_ARGS__})

void mel_gpu_buffer_shutdown(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev);

void* mel_gpu_buffer_map(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev);
void mel_gpu_buffer_unmap(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev);
void mel_gpu_buffer_flush(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev);

void mel_gpu_buffer_upload(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev,
                           const void* data, u64 size, u64 offset);
