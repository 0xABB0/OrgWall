#define VK_NO_PROTOTYPES
#include "vk_buffer.h"
#include <string.h>

bool mel_vk_buffer_init(Mel_VkBuffer* buf, Mel_VkContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage)
{
    assert(buf != nullptr);
    assert(ctx != nullptr);
    assert(size > 0);

    *buf = (Mel_VkBuffer){0};

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info = {
        .usage = memory_usage,
    };

    if (memory_usage == VMA_MEMORY_USAGE_CPU_TO_GPU || memory_usage == VMA_MEMORY_USAGE_CPU_ONLY)
    {
        alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    VmaAllocationInfo allocation_info;
    VkResult result = vmaCreateBuffer(ctx->vma, &buffer_info, &alloc_info, &buf->buffer, &buf->allocation, &allocation_info);
    if (result != VK_SUCCESS)
    {
        return false;
    }

    buf->size = size;
    buf->mapped = allocation_info.pMappedData;

    return true;
}

void mel_vk_buffer_shutdown(Mel_VkBuffer* buf, Mel_VkContext* ctx)
{
    assert(buf != nullptr);
    assert(ctx != nullptr);

    if (buf->buffer)
    {
        vmaDestroyBuffer(ctx->vma, buf->buffer, buf->allocation);
        buf->buffer = VK_NULL_HANDLE;
        buf->allocation = VK_NULL_HANDLE;
        buf->mapped = nullptr;
    }
}

void* mel_vk_buffer_map(Mel_VkBuffer* buf, Mel_VkContext* ctx)
{
    assert(buf != nullptr);
    assert(ctx != nullptr);

    if (buf->mapped)
    {
        return buf->mapped;
    }

    vmaMapMemory(ctx->vma, buf->allocation, &buf->mapped);
    return buf->mapped;
}

void mel_vk_buffer_unmap(Mel_VkBuffer* buf, Mel_VkContext* ctx)
{
    assert(buf != nullptr);
    assert(ctx != nullptr);

    if (buf->mapped)
    {
        vmaUnmapMemory(ctx->vma, buf->allocation);
        buf->mapped = nullptr;
    }
}

void mel_vk_buffer_flush(Mel_VkBuffer* buf, Mel_VkContext* ctx)
{
    assert(buf != nullptr);
    assert(ctx != nullptr);

    vmaFlushAllocation(ctx->vma, buf->allocation, 0, buf->size);
}

void mel_vk_buffer_upload(Mel_VkBuffer* buf, Mel_VkContext* ctx, const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    assert(buf != nullptr);
    assert(ctx != nullptr);
    assert(data != nullptr);
    assert(offset + size <= buf->size);

    void* mapped = mel_vk_buffer_map(buf, ctx);
    memcpy((u8*)mapped + offset, data, size);
    mel_vk_buffer_flush(buf, ctx);
}
