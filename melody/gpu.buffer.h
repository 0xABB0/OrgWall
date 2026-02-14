#pragma once

#include "gpu.device.h"

typedef struct Mel_Gpu_Buffer Mel_Gpu_Buffer;

struct Mel_Gpu_Buffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VkDeviceSize size;
    void* mapped;
    VkDeviceAddress device_address;
    VkBufferUsageFlags usage;
    VkPipelineStageFlags2 current_stage;
    VkAccessFlags2 current_access;
};

typedef struct {
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VmaMemoryUsage memory_usage;
    bool map_on_create;
} Mel_Gpu_Buffer_Opt;

void mel_gpu_buffer_init_opt(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt);
#define mel_gpu_buffer_init(buf, dev, ...) mel_gpu_buffer_init_opt((buf), (dev), (Mel_Gpu_Buffer_Opt){__VA_ARGS__})

void mel_gpu_buffer_shutdown(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev);

void* mel_gpu_buffer_map(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev);
void mel_gpu_buffer_unmap(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev);
void mel_gpu_buffer_flush(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev);

void mel_gpu_buffer_upload(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev,
                           const void* data, VkDeviceSize size, VkDeviceSize offset);
