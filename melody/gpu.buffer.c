#define VK_NO_PROTOTYPES
#include "gpu.buffer.h"
#include <string.h>

void mel_gpu_buffer_init_opt(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    assert(buf != nullptr);
    assert(dev != nullptr);
    assert(opt.size > 0);

    *buf = (Mel_Gpu_Buffer){0};

    VkBufferUsageFlags usage = opt.usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = opt.size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info = {
        .usage = opt.memory_usage ? opt.memory_usage : VMA_MEMORY_USAGE_GPU_ONLY,
    };

    if (alloc_info.usage == VMA_MEMORY_USAGE_CPU_TO_GPU || alloc_info.usage == VMA_MEMORY_USAGE_CPU_ONLY)
        alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VmaAllocationInfo allocation_info;
    VkResult r = vmaCreateBuffer(dev->vma, &buffer_info, &alloc_info, &buf->buffer, &buf->allocation, &allocation_info);
    assert(r == VK_SUCCESS);

    buf->size = opt.size;
    buf->usage = usage;
    buf->mapped = allocation_info.pMappedData;
    buf->current_stage = VK_PIPELINE_STAGE_2_NONE;
    buf->current_access = VK_ACCESS_2_NONE;

    VkBufferDeviceAddressInfo addr_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buf->buffer,
    };
    buf->device_address = vkGetBufferDeviceAddress(dev->device, &addr_info);
}

void mel_gpu_buffer_shutdown(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev)
{
    assert(buf != nullptr);
    assert(dev != nullptr);

    if (buf->buffer)
    {
        vmaDestroyBuffer(dev->vma, buf->buffer, buf->allocation);
        buf->buffer = VK_NULL_HANDLE;
        buf->allocation = VK_NULL_HANDLE;
        buf->mapped = nullptr;
        buf->device_address = 0;
    }
}

void* mel_gpu_buffer_map(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev)
{
    assert(buf != nullptr);
    assert(dev != nullptr);
    assert(buf->mapped == nullptr);

    vmaMapMemory(dev->vma, buf->allocation, &buf->mapped);
    return buf->mapped;
}

void mel_gpu_buffer_unmap(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev)
{
    assert(buf != nullptr);
    assert(dev != nullptr);
    assert(buf->mapped != nullptr);

    vmaUnmapMemory(dev->vma, buf->allocation);
    buf->mapped = nullptr;
}

void mel_gpu_buffer_flush(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev)
{
    assert(buf != nullptr);
    assert(dev != nullptr);

    vmaFlushAllocation(dev->vma, buf->allocation, 0, buf->size);
}

void mel_gpu_buffer_upload(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev,
                           const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    assert(buf != nullptr);
    assert(dev != nullptr);
    assert(data != nullptr);
    assert(offset + size <= buf->size);

    bool was_mapped = buf->mapped != nullptr;
    void* mapped = was_mapped ? buf->mapped : mel_gpu_buffer_map(buf, dev);
    memcpy((u8*)mapped + offset, data, size);
    mel_gpu_buffer_flush(buf, dev);
    if (!was_mapped)
        mel_gpu_buffer_unmap(buf, dev);
}
