#include "vk_backend.h"

#include <gpu/memory.h>
#include <allocator/buddy.h>
#include <log/log.h>

#include <stdatomic.h>

#define MEL_GPU_BLOCK_SIZE          (64ull * 1024 * 1024)
#define MEL_GPU_DEDICATED_THRESHOLD (32ull * 1024 * 1024)
#define MEL_GPU_MIN_BLOCK           256ull

typedef struct Mel_Gpu_Mem_Block
{
    VkDeviceMemory            mem;
    u32                       type_index;
    Mel_Buddy_Alloc           buddy;
    u8*                       tree;
    void*                     mapped;
    struct Mel_Gpu_Mem_Block* next;
} Mel_Gpu_Mem_Block;

struct Mel_Gpu_Allocator
{
    Mel_Gpu_Device*    dev;
    Mel_Mutex          lock;
    Mel_Gpu_Mem_Block* blocks;
    _Atomic(u64)       used;
};

static void mel_gpu__usage_add(Mel_Gpu_Device* dev, u64 bytes)
{
    u64 used = atomic_fetch_add(&dev->allocator->used, bytes) + bytes;
    u64 budget = dev->caps.memory.device_local_bytes;
    if (budget && used > budget && dev->budget_pressure_cb)
        dev->budget_pressure_cb(dev, (Mel_Gpu_Memory_Budget){ .budget_bytes = budget, .usage_bytes = used }, dev->budget_pressure_user);
}

static u64 mel_gpu__buddy_block_bytes(u64 size)
{
    if (size <= MEL_GPU_MIN_BLOCK)
        return MEL_GPU_MIN_BLOCK;
    u64 p = MEL_GPU_MIN_BLOCK;
    while (p < size)
        p <<= 1;
    return p;
}

void mel_gpu__allocator_init(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Allocator* a = mel_alloc_type(dev->alloc, Mel_Gpu_Allocator);
    *a = (Mel_Gpu_Allocator){ 0 };
    a->dev = dev;
    mel_mutex_init(&a->lock, MEL_MUTEX_PLAIN);
    dev->allocator = a;
}

void mel_gpu__allocator_shutdown(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Allocator* a = dev->allocator;
    if (!a)
        return;
    for (Mel_Gpu_Mem_Block* b = a->blocks; b;)
    {
        Mel_Gpu_Mem_Block* next = b->next;
        if (b->mapped)
            vkUnmapMemory(dev->vk, b->mem);
        vkFreeMemory(dev->vk, b->mem, NULL);
        mel_dealloc(dev->alloc, b->tree);
        mel_dealloc(dev->alloc, b);
        b = next;
    }
    mel_mutex_destroy(&a->lock);
    mel_dealloc(dev->alloc, a);
    dev->allocator = NULL;
}

static u32 mel_gpu__log2(usize v)
{
    u32 n = 0;
    while (v > 1)
    {
        v >>= 1;
        n++;
    }
    return n;
}

static Mel_Gpu_Mem_Block* mel_gpu__block_create(Mel_Gpu_Device* dev, u32 type_index, bool host_visible)
{
    VkMemoryAllocateFlagsInfo flags = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT };
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = dev->bda_enabled ? &flags : NULL,
        .allocationSize = MEL_GPU_BLOCK_SIZE,
        .memoryTypeIndex = type_index,
    };
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkResult       r = vkAllocateMemory(dev->vk, &ai, NULL, &mem);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkAllocateMemory(block) failed: %s", mel_gpu__vk_result_str(r));
        return NULL;
    }

    Mel_Gpu_Mem_Block* b = mel_alloc_type(dev->alloc, Mel_Gpu_Mem_Block);
    *b = (Mel_Gpu_Mem_Block){ 0 };
    b->mem = mem;
    b->type_index = type_index;

    u32   levels = mel_gpu__log2(MEL_GPU_BLOCK_SIZE / MEL_GPU_MIN_BLOCK) + 1;
    usize tree_size = (1u << levels) - 1;
    b->tree = mel_alloc(dev->alloc, tree_size);
    mel_buddy_init(&b->buddy, (void*)(usize)MEL_GPU_MIN_BLOCK, MEL_GPU_BLOCK_SIZE, .min_block_size = MEL_GPU_MIN_BLOCK, .tree_buffer = b->tree);

    if (host_visible)
        vkMapMemory(dev->vk, mem, 0, VK_WHOLE_SIZE, 0, &b->mapped);

    b->next = dev->allocator->blocks;
    dev->allocator->blocks = b;
    return b;
}

bool mel_gpu__mem_alloc(Mel_Gpu_Device* dev, VkMemoryRequirements req, VkMemoryPropertyFlags props, bool force_dedicated, Mel_Gpu_Allocation* out)
{
    *out = (Mel_Gpu_Allocation){ 0 };

    u32 type = mel_gpu__vk_find_memory_type(dev, req.memoryTypeBits, props);
    if (type == UINT32_MAX)
    {
        mel_log_error("gpu", "no memory type for props 0x%x", (unsigned)props);
        return false;
    }

    bool host_visible = (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    bool dedicated = force_dedicated || req.size >= MEL_GPU_DEDICATED_THRESHOLD || req.alignment > MEL_GPU_MIN_BLOCK;

    if (dedicated)
    {
        VkMemoryAllocateFlagsInfo flags = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT };
        VkMemoryAllocateInfo      ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = dev->bda_enabled ? &flags : NULL,
            .allocationSize = req.size,
            .memoryTypeIndex = type,
        };
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkResult       r = vkAllocateMemory(dev->vk, &ai, NULL, &mem);
        if (r != VK_SUCCESS)
        {
            mel_log_error("gpu", "vkAllocateMemory(dedicated %llu) failed: %s", (unsigned long long)req.size, mel_gpu__vk_result_str(r));
            return false;
        }
        void* mapped = NULL;
        if (host_visible)
            vkMapMemory(dev->vk, mem, 0, VK_WHOLE_SIZE, 0, &mapped);
        out->mem = mem;
        out->offset = 0;
        out->size = req.size;
        out->mapped = mapped;
        out->block = NULL;
        mel_gpu__usage_add(dev, req.size);
        return true;
    }

    Mel_Gpu_Allocator* a = dev->allocator;
    mel_mutex_lock(&a->lock);

    for (Mel_Gpu_Mem_Block* b = a->blocks; b; b = b->next)
    {
        if (b->type_index != type)
            continue;
        void* ptr = mel_buddy_alloc(&b->buddy, req.size);
        if (!ptr)
            continue;
        VkDeviceSize offset = (VkDeviceSize)((u8*)ptr - b->buddy.base);
        u64          consumed = mel_gpu__buddy_block_bytes(req.size);
        out->mem = b->mem;
        out->offset = offset;
        out->size = consumed;
        out->mapped = b->mapped ? (u8*)b->mapped + offset : NULL;
        out->block = b;
        mel_mutex_unlock(&a->lock);
        mel_gpu__usage_add(dev, consumed);
        return true;
    }

    Mel_Gpu_Mem_Block* b = mel_gpu__block_create(dev, type, host_visible);
    if (!b)
    {
        mel_mutex_unlock(&a->lock);
        return false;
    }
    void* ptr = mel_buddy_alloc(&b->buddy, req.size);
    if (!ptr)
    {
        mel_mutex_unlock(&a->lock);
        mel_log_error("gpu", "suballocation of %llu failed in fresh block", (unsigned long long)req.size);
        return false;
    }
    VkDeviceSize offset = (VkDeviceSize)((u8*)ptr - b->buddy.base);
    u64          consumed = mel_gpu__buddy_block_bytes(req.size);
    out->mem = b->mem;
    out->offset = offset;
    out->size = consumed;
    out->mapped = b->mapped ? (u8*)b->mapped + offset : NULL;
    out->block = b;
    mel_mutex_unlock(&a->lock);
    mel_gpu__usage_add(dev, consumed);
    return true;
}

Mel_Gpu_Memory_Budget mel_gpu_memory_budget(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Memory_Budget b = { 0 };
    if (!dev)
        return b;

    if (dev->has_memory_budget)
    {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT bp = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
        VkPhysicalDeviceMemoryProperties2         mp = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, .pNext = &bp };
        vkGetPhysicalDeviceMemoryProperties2(dev->phys, &mp);
        for (u32 i = 0; i < mp.memoryProperties.memoryHeapCount; i++)
        {
            if (mp.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                b.budget_bytes += bp.heapBudget[i];
                b.usage_bytes += bp.heapUsage[i];
            }
        }
        return b;
    }

    b.budget_bytes = dev->caps.memory.device_local_bytes;
    b.usage_bytes = atomic_load(&dev->allocator->used);
    return b;
}

void mel_gpu_set_budget_pressure_callback(Mel_Gpu_Device* dev, Mel_Gpu_Budget_Pressure_Fn cb, void* user)
{
    if (!dev)
        return;
    dev->budget_pressure_cb = cb;
    dev->budget_pressure_user = user;
}

void mel_gpu__mem_free(Mel_Gpu_Device* dev, Mel_Gpu_Allocation* a)
{
    if (!a || a->mem == VK_NULL_HANDLE)
        return;

    atomic_fetch_sub(&dev->allocator->used, a->size);

    if (a->block == NULL)
    {
        vkFreeMemory(dev->vk, a->mem, NULL);
    }
    else
    {
        Mel_Gpu_Mem_Block* b = a->block;
        mel_mutex_lock(&dev->allocator->lock);
        mel_buddy_free(&b->buddy, b->buddy.base + a->offset);
        mel_mutex_unlock(&dev->allocator->lock);
    }
    *a = (Mel_Gpu_Allocation){ 0 };
}
