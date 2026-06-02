#include "vk_backend.h"

#include <gpu/buffer.h>
#include <log/log.h>

#include <string.h>

static VkBufferUsageFlags mel_gpu__buffer_usage(Mel_Gpu_Buffer_Usage u)
{
    VkBufferUsageFlags f = 0;
    if (u & MEL_GPU_BUFFER_VERTEX)
        f |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (u & MEL_GPU_BUFFER_INDEX)
        f |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (u & MEL_GPU_BUFFER_UNIFORM)
        f |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (u & MEL_GPU_BUFFER_STORAGE)
        f |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (u & MEL_GPU_BUFFER_TRANSFER_SRC)
        f |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (u & MEL_GPU_BUFFER_TRANSFER_DST)
        f |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (u & MEL_GPU_BUFFER_DEVICE_ADDRESS)
        f |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (u & MEL_GPU_BUFFER_INDIRECT)
        f |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    return f;
}

static bool mel_gpu__staging_upload(Mel_Gpu_Device* dev, VkBuffer dst, const void* data, usize size)
{
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(dev->vk, &bci, NULL, &staging) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev->vk, staging, &req);
    Mel_Gpu_Allocation sa;
    if (!mel_gpu__mem_alloc(dev, req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &sa))
    {
        vkDestroyBuffer(dev->vk, staging, NULL);
        return false;
    }
    vkBindBufferMemory(dev->vk, staging, sa.mem, sa.offset);
    memcpy(sa.mapped, data, size);

    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = dev->graphics_family };
    VkCommandPool           pool = VK_NULL_HANDLE;
    vkCreateCommandPool(dev->vk, &pci, NULL, &pool);

    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer             cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev->vk, &cai, &cb);

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cb, &bi);
    VkBufferCopy copy = { .size = size };
    vkCmdCopyBuffer(cb, staging, dst, 1, &copy);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    mel_mutex_lock(&dev->submit_lock);
    vkQueueSubmit(dev->graphics_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(dev->graphics_queue);
    mel_mutex_unlock(&dev->submit_lock);

    vkDestroyCommandPool(dev->vk, pool, NULL);
    vkDestroyBuffer(dev->vk, staging, NULL);
    mel_gpu__mem_free(dev, &sa);
    return true;
}

Mel_Gpu_Buffer_Create_Result mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    Mel_Gpu_Buffer_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_BUFFER_CREATE_OK };

    if (!dev || opt.size == 0)
    {
        res.status = MEL_GPU_BUFFER_CREATE_BAD_PARAMS;
        return res;
    }

    bool               device_local = opt.memory == MEL_GPU_MEMORY_DEVICE;
    VkBufferUsageFlags usage = mel_gpu__buffer_usage(opt.usage);
    if (device_local && opt.data)
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = opt.size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer buf = VK_NULL_HANDLE;
    VkResult r = vkCreateBuffer(dev->vk, &bci, NULL, &buf);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkCreateBuffer failed: %s", mel_gpu__vk_result_str(r));
        res.status = MEL_GPU_BUFFER_CREATE_VK_FAILED;
        return res;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev->vk, buf, &req);

    VkMemoryPropertyFlags props = device_local ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    Mel_Gpu_Buffer_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.capture_replay = opt.capture_replay;
    obj.header.name = opt.name;
    obj.buf = buf;
    obj.size = opt.size;
    obj.host_visible = !device_local;

    if (!mel_gpu__mem_alloc(dev, req, props, false, &obj.alloc))
    {
        vkDestroyBuffer(dev->vk, buf, NULL);
        res.status = MEL_GPU_BUFFER_CREATE_OOM;
        return res;
    }
    vkBindBufferMemory(dev->vk, buf, obj.alloc.mem, obj.alloc.offset);

    if (opt.data)
    {
        if (device_local)
        {
            if (!mel_gpu__staging_upload(dev, buf, opt.data, opt.size))
            {
                mel_gpu__mem_free(dev, &obj.alloc);
                vkDestroyBuffer(dev->vk, buf, NULL);
                res.status = MEL_GPU_BUFFER_CREATE_VK_FAILED;
                return res;
            }
        }
        else
        {
            memcpy(obj.alloc.mapped, opt.data, opt.size);
        }
    }

    res.value.slot = mel_gpu__table_insert(dev, &dev->buffers, &obj);

    // U14: auto-register the buffer into the device bindless heap at its handle index (direct contract §3.1)
    // for every shader-addressable class its usage admits (gpu-rhi.md §6.7).
    if (dev->bindless.enabled)
    {
        if (opt.usage & MEL_GPU_BUFFER_STORAGE)
            mel_gpu__bindless_register_storage_buffer(dev, res.value.slot.index, buf, opt.size);
        if (opt.usage & MEL_GPU_BUFFER_UNIFORM)
            mel_gpu__bindless_register_uniform_buffer(dev, res.value.slot.index, buf, opt.size);
    }
    return res;
}

void mel_gpu_buffer_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o)
        return;
    bool               borrowed = o->header.ownership == MEL_GPU_OWNERSHIP_BORROWED;
    Mel_Gpu_Allocation alloc = o->alloc;
    VkBuffer           vk = o->buf;
    if (borrowed)
    {
        // Imported (Borrowed): no underlying free, not heap-registered — the slot reuses immediately.
        mel_gpu__table_remove(dev, &dev->buffers, buf.slot);
        return;
    }
    // U3/U14 future-gated retirement: the generation rolls now (use-after-free stays loud), but both the
    // Vulkan object and the bindless slot index are reclaimed only once in-flight submissions retire.
    mel_gpu__table_remove_deferred(dev, &dev->buffers, buf.slot);
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .buffer = vk, .alloc = alloc, .has_alloc = true, .reclaim_table = &dev->buffers, .reclaim_index = buf.slot.index, .has_reclaim = true });
}

u32 mel_gpu_buffer_make_resident(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    (void)buf;
    if (dev->caps.memory.residency_control < MEL_GPU_RESIDENCY_EXPLICIT)
    {
        mel_log_warn("gpu", "make_resident: explicit residency unavailable on this device; no-op");
        return MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_WARNED);
    }
    return MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK);
}

u32 mel_gpu_buffer_evict(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    (void)buf;
    if (dev->caps.memory.residency_control < MEL_GPU_RESIDENCY_EXPLICIT)
    {
        mel_log_warn("gpu", "evict: explicit residency unavailable on this device; no-op");
        return MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_WARNED);
    }
    return MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK);
}

Mel_Gpu_Buffer mel_gpu_buffer_import(Mel_Gpu_Device* dev, void* native_buffer, usize size, const char* name)
{
    Mel_Gpu_Buffer_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_BORROWED;
    obj.header.name = name;
    obj.buf = (VkBuffer)native_buffer;
    obj.size = size;
    obj.host_visible = false;
    obj.alloc = (Mel_Gpu_Allocation){ 0 };
    Mel_Gpu_Buffer h = { mel_gpu__table_insert(dev, &dev->buffers, &obj) };
    return h;
}

bool mel_gpu_buffer_alive(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf) { return mel_gpu__table_get(dev, &dev->buffers, buf.slot) != NULL; }

void mel_gpu_buffer_write(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, const void* data, usize bytes)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    mel_assert(o != NULL);
    if (!o)
        return;
    if (o->host_visible && o->alloc.mapped)
        memcpy(o->alloc.mapped, data, bytes);
    else
        mel_gpu__staging_upload(dev, o->buf, data, bytes);
}

void* mel_gpu_buffer_mapped(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    return o ? o->alloc.mapped : NULL;
}

bool mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, VkBuffer* out)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o)
        return false;
    *out = o->buf;
    return true;
}

u64 mel_gpu_buffer_device_address(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    if (!dev || !dev->bda_enabled)
    {
        mel_log_error("gpu", "buffer_device_address: device was not created with buffer_device_address");
        return 0;
    }
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o)
    {
        mel_assert(!"buffer_device_address: invalid buffer handle");
        return 0;
    }
    VkBufferDeviceAddressInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = o->buf };
    return (u64)vkGetBufferDeviceAddress(dev->vk, &info);
}
