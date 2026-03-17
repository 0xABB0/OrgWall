#include "gpu.buffer.h"
#include "gpu.device.h"
#include "gpu.types.vulkan.h"
#include <string.h>
#include <tracy/TracyC.h>

void mel_gpu_buffer_init_opt(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    assert(buf != nullptr);
    assert(dev != nullptr);
    assert(opt.size > 0);

    TracyCZoneN(ctx, "gpu_buffer_init", true);
    *buf = (Mel_Gpu_Buffer){0};

    VkBufferUsageFlags usage = mel__gpu_buffer_usage_to_vk(opt.usage) | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = opt.size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaMemoryUsage vma_usage = opt.memory_usage
        ? mel__gpu_memory_usage_to_vma(opt.memory_usage)
        : VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocationCreateInfo alloc_info = {
        .usage = vma_usage,
    };

    if (vma_usage == VMA_MEMORY_USAGE_CPU_TO_GPU || vma_usage == VMA_MEMORY_USAGE_CPU_ONLY)
        alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VmaAllocationInfo allocation_info;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocation vma_alloc = VK_NULL_HANDLE;
    VkResult r = vmaCreateBuffer(dev->vma, &buffer_info, &alloc_info, &vk_buffer, &vma_alloc, &allocation_info);
    assert(r == VK_SUCCESS);

    buf->_handle = vk_buffer;
    buf->_allocation = vma_alloc;
    buf->size = opt.size;
    buf->usage = opt.usage | MEL_GPU_BUFFER_USAGE_DEVICE_ADDRESS;
    buf->mapped = allocation_info.pMappedData;
    buf->current_stage = MEL_GPU_STAGE_NONE;
    buf->current_access = MEL_GPU_ACCESS_NONE;

    VkBufferDeviceAddressInfo addr_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = vk_buffer,
    };
    buf->device_address = vkGetBufferDeviceAddress(dev->device, &addr_info);
    TracyCZoneEnd(ctx);
}

void mel_gpu_buffer_shutdown(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev)
{
    assert(buf != nullptr);
    assert(dev != nullptr);

    if (buf->_handle)
    {
        vmaDestroyBuffer(dev->vma, (VkBuffer)buf->_handle, (VmaAllocation)buf->_allocation);
        buf->_handle = nullptr;
        buf->_allocation = nullptr;
        buf->mapped = nullptr;
        buf->device_address = 0;
    }
}

void* mel_gpu_buffer_map(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev)
{
    assert(buf != nullptr);
    assert(dev != nullptr);
    assert(buf->mapped == nullptr);

    vmaMapMemory(dev->vma, (VmaAllocation)buf->_allocation, &buf->mapped);
    return buf->mapped;
}

void mel_gpu_buffer_unmap(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev)
{
    assert(buf != nullptr);
    assert(dev != nullptr);
    assert(buf->mapped != nullptr);

    vmaUnmapMemory(dev->vma, (VmaAllocation)buf->_allocation);
    buf->mapped = nullptr;
}

void mel_gpu_buffer_flush(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev)
{
    assert(buf != nullptr);
    assert(dev != nullptr);

    vmaFlushAllocation(dev->vma, (VmaAllocation)buf->_allocation, 0, buf->size);
}

void mel_gpu_buffer_upload(Mel_Gpu_Buffer* buf, Mel_Gpu_Device* dev,
                           const void* data, u64 size, u64 offset)
{
    assert(buf != nullptr);
    assert(dev != nullptr);
    assert(data != nullptr);
    assert(offset + size <= buf->size);

    TracyCZoneN(ctx, "gpu_buffer_upload", true);
    bool was_mapped = buf->mapped != nullptr;
    void* mapped = was_mapped ? buf->mapped : mel_gpu_buffer_map(buf, dev);
    memcpy((u8*)mapped + offset, data, size);
    mel_gpu_buffer_flush(buf, dev);
    if (!was_mapped)
        mel_gpu_buffer_unmap(buf, dev);
    TracyCZoneEnd(ctx);
}
