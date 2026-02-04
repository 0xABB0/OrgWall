#ifndef MEL_VK_BUFFER_H
#define MEL_VK_BUFFER_H

#include "vk_context.h"

typedef struct
{
    VkBuffer buffer;
    VmaAllocation allocation;
    VkDeviceSize size;
    void* mapped;
} Mel_VkBuffer;

bool mel_vk_buffer_init(Mel_VkBuffer* buf, Mel_VkContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage);
void mel_vk_buffer_shutdown(Mel_VkBuffer* buf, Mel_VkContext* ctx);

void* mel_vk_buffer_map(Mel_VkBuffer* buf, Mel_VkContext* ctx);
void mel_vk_buffer_unmap(Mel_VkBuffer* buf, Mel_VkContext* ctx);
void mel_vk_buffer_flush(Mel_VkBuffer* buf, Mel_VkContext* ctx);

void mel_vk_buffer_upload(Mel_VkBuffer* buf, Mel_VkContext* ctx, const void* data, VkDeviceSize size, VkDeviceSize offset);

#endif
