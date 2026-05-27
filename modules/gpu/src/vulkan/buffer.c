#include "vulkan_backend.h"

static bool find_memory_type(VkPhysicalDevice phys, u32 type_bits, VkMemoryPropertyFlags want, u32* out)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (u32 i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
            *out = i;
            return true;
        }
    }
    return false;
}

static VkBuffer create_raw(Mel_Gpu_Device* dev, usize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags props, VkDeviceMemory* out_mem)
{
    VkBufferCreateInfo bci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer buf;
    if (vkCreateBuffer(dev->device, &bci, NULL, &buf) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev->device, buf, &req);

    u32 type_index;
    if (!find_memory_type(dev->phys, req.memoryTypeBits, props, &type_index)) {
        vkDestroyBuffer(dev->device, buf, NULL);
        return VK_NULL_HANDLE;
    }
    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = type_index,
    };
    if (vkAllocateMemory(dev->device, &mai, NULL, out_mem) != VK_SUCCESS) {
        vkDestroyBuffer(dev->device, buf, NULL);
        return VK_NULL_HANDLE;
    }
    vkBindBufferMemory(dev->device, buf, *out_mem, 0);
    return buf;
}

static void upload_via_staging(Mel_Gpu_Device* dev, VkBuffer dst, const void* data, usize size)
{
    VkDeviceMemory staging_mem;
    VkBuffer staging = create_raw(dev, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &staging_mem);
    if (!staging) return;

    void* p;
    vkMapMemory(dev->device, staging_mem, 0, size, 0, &p);
    memcpy(p, data, size);
    vkUnmapMemory(dev->device, staging_mem);

    VkCommandBufferAllocateInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = dev->cmd_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev->device, &ai, &cb);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cb, &bi);
    VkBufferCopy copy = { .size = size };
    vkCmdCopyBuffer(cb, staging, dst, 1, &copy);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    vkQueueSubmit(dev->queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(dev->queue);

    vkFreeCommandBuffers(dev->device, dev->cmd_pool, 1, &cb);
    vkDestroyBuffer(dev->device, staging, NULL);
    vkFreeMemory(dev->device, staging_mem, NULL);
}

Mel_Gpu_Buffer* mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    if (!dev || opt.size == 0) return NULL;

    VkBufferUsageFlags usage = 0;
    if (opt.usage & MEL_GPU_BUFFER_VERTEX)  usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (opt.usage & MEL_GPU_BUFFER_INDEX)   usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (opt.usage & MEL_GPU_BUFFER_UNIFORM) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    bool host_visible = (opt.memory != MEL_GPU_MEMORY_GPU_ONLY);
    if (!host_visible) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkMemoryPropertyFlags props = host_visible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkDeviceMemory mem;
    VkBuffer buf = create_raw(dev, opt.size, usage, props, &mem);
    if (!buf) return NULL;

    if (opt.data) {
        if (host_visible) {
            void* p;
            vkMapMemory(dev->device, mem, 0, opt.size, 0, &p);
            memcpy(p, opt.data, opt.size);
            vkUnmapMemory(dev->device, mem);
        } else {
            upload_via_staging(dev, buf, opt.data, opt.size);
        }
    }

    Mel_Gpu_Buffer* b = calloc(1, sizeof *b);
    if (!b) { vkDestroyBuffer(dev->device, buf, NULL); vkFreeMemory(dev->device, mem, NULL); return NULL; }
    b->device       = dev;
    b->buf          = buf;
    b->mem          = mem;
    b->size         = opt.size;
    b->host_visible = host_visible;
    return b;
}

void mel_gpu_buffer_destroy(Mel_Gpu_Buffer* buf)
{
    if (!buf) return;
    vkDeviceWaitIdle(buf->device->device);
    vkDestroyBuffer(buf->device->device, buf->buf, NULL);
    vkFreeMemory(buf->device->device, buf->mem, NULL);
    free(buf);
}

void* mel_gpu_buffer_map(Mel_Gpu_Buffer* buf)
{
    (void)buf;
    return NULL; // persistent mapping not exposed; use mel_gpu_buffer_write
}

void mel_gpu_buffer_write(Mel_Gpu_Buffer* buf, const void* data, usize size)
{
    if (!buf || !buf->host_visible || !data || size == 0) return;
    if (size > buf->size) size = buf->size;
    void* p;
    if (vkMapMemory(buf->device->device, buf->mem, 0, size, 0, &p) != VK_SUCCESS) return;
    memcpy(p, data, size);
    vkUnmapMemory(buf->device->device, buf->mem);
}
