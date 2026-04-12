#include "gpu.scratch_pool.h"
#include "gpu.device.vulkan.h"
#include "gpu.types.vulkan.h"
#include "allocator.heap.h"

#include <string.h>

static VkDeviceSize mel__scratch_image_size(Mel_Gpu_Device* dev, Mel_Scratch_Desc desc)
{
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = mel__gpu_format_to_vk(desc.format),
        .extent = { desc.width, desc.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = desc.usage ? mel__gpu_image_usage_to_vk(desc.usage) : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkMemoryRequirements reqs;
    VkImage tmp;
    VkResult r = vkCreateImage(mel__gpu_device_vk(dev)->device, &info, nullptr, &tmp);
    assert(r == VK_SUCCESS);
    vkGetImageMemoryRequirements(mel__gpu_device_vk(dev)->device, tmp, &reqs);
    vkDestroyImage(mel__gpu_device_vk(dev)->device, tmp, nullptr);
    return reqs.size;
}

static u32 mel__scratch_find_memory_type(Mel_Gpu_Device* dev, u32 type_bits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(mel__gpu_device_vk(dev)->physical_device, &mem_props);

    for (u32 i = 0; i < mem_props.memoryTypeCount; i++)
    {
        if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    assert(false && "no suitable memory type");
    return 0;
}

static Mel_Scratch_Block mel__scratch_block_create(Mel_Gpu_Device* dev, VkDeviceSize size,
                                                    u32 memory_type_index, const Mel_Alloc* alloc)
{
    Mel_Scratch_Block block = {0};
    block.size = size;

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = size,
        .memoryTypeIndex = memory_type_index,
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult r = vkAllocateMemory(mel__gpu_device_vk(dev)->device, &alloc_info, nullptr, &memory);
    assert(r == VK_SUCCESS);
    block._memory = memory;

    block.image_capacity = 16;
    block.images = mel_alloc_array(alloc, Mel_Scratch_Image, block.image_capacity);

    return block;
}

static void mel__scratch_block_destroy(Mel_Gpu_Device* dev, Mel_Scratch_Block* block, const Mel_Alloc* alloc)
{
    for (u32 i = 0; i < block->image_count; i++)
    {
        Mel_Scratch_Image* si = &block->images[i];
        if (si->image._view)
            vkDestroyImageView(mel__gpu_device_vk(dev)->device, (VkImageView)si->image._view, nullptr);
        if (si->image._handle)
            vkDestroyImage(mel__gpu_device_vk(dev)->device, (VkImage)si->image._handle, nullptr);
        if (si->image.subresource_states)
            mel_dealloc(alloc, si->image.subresource_states);
    }
    if (block->_memory)
        vkFreeMemory(mel__gpu_device_vk(dev)->device, (VkDeviceMemory)block->_memory, nullptr);
    if (block->images)
        mel_dealloc(alloc, block->images);
    *block = (Mel_Scratch_Block){0};
}

void mel_scratch_pool_init_opt(Mel_Scratch_Pool* pool, Mel_Scratch_Pool_Opt opt)
{
    assert(pool != nullptr);
    assert(opt.dev != nullptr);

    *pool = (Mel_Scratch_Pool){0};
    pool->dev = opt.dev;
    pool->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    pool->block_capacity = 8;
    pool->blocks = mel_alloc_array(pool->alloc, Mel_Scratch_Block, pool->block_capacity);
}

void mel_scratch_pool_shutdown(Mel_Scratch_Pool* pool)
{
    assert(pool != nullptr);

    for (u32 i = 0; i < pool->block_count; i++)
        mel__scratch_block_destroy(pool->dev, &pool->blocks[i], pool->alloc);

    if (pool->blocks)
        mel_dealloc(pool->alloc, pool->blocks);

    *pool = (Mel_Scratch_Pool){0};
}

static Mel_Scratch_Image mel__scratch_image_create(Mel_Gpu_Device* dev, Mel_Scratch_Block* block,
                                                     Mel_Scratch_Desc desc, const Mel_Alloc* alloc)
{
    Mel_Scratch_Image si = {0};
    si.block = block;

    VkImageUsageFlags usage = desc.usage
        ? mel__gpu_image_usage_to_vk(desc.usage)
        : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    VkImageAspectFlags aspect = desc.aspect
        ? mel__gpu_aspect_to_vk(desc.aspect)
        : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = mel__gpu_format_to_vk(desc.format),
        .extent = { desc.width, desc.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage vk_image = VK_NULL_HANDLE;
    VkResult r = vkCreateImage(mel__gpu_device_vk(dev)->device, &image_info, nullptr, &vk_image);
    assert(r == VK_SUCCESS);

    r = vkBindImageMemory(mel__gpu_device_vk(dev)->device, vk_image, (VkDeviceMemory)block->_memory, 0);
    assert(r == VK_SUCCESS);

    VkImageView vk_view = VK_NULL_HANDLE;
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = mel__gpu_format_to_vk(desc.format),
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    r = vkCreateImageView(mel__gpu_device_vk(dev)->device, &view_info, nullptr, &vk_view);
    assert(r == VK_SUCCESS);

    si.image._handle = vk_image;
    si.image._view = vk_view;
    si.image._allocation = nullptr;
    si.image.format = desc.format;
    si.image.width = desc.width;
    si.image.height = desc.height;
    si.image.mip_levels = 1;
    si.image.layer_count = 1;
    si.image.aspect = desc.aspect ? desc.aspect : MEL_GPU_ASPECT_COLOR;
    si.image.alloc = alloc;

    si.image.subresource_states = mel_alloc_type(alloc, Mel_Gpu_Image_State);
    *si.image.subresource_states = (Mel_Gpu_Image_State){
        .layout = MEL_GPU_IMAGE_LAYOUT_UNDEFINED,
        .stage = MEL_GPU_STAGE_NONE,
        .access = MEL_GPU_ACCESS_NONE,
    };

    return si;
}

Mel_Gpu_Image mel_scratch_acquire_opt(Mel_Scratch_Pool* pool, Mel_Scratch_Desc desc)
{
    assert(pool != nullptr);
    assert(desc.width > 0);
    assert(desc.height > 0);
    assert(desc.format != MEL_GPU_FORMAT_UNDEFINED);

    for (u32 bi = 0; bi < pool->block_count; bi++)
    {
        Mel_Scratch_Block* block = &pool->blocks[bi];

        for (u32 ii = 0; ii < block->image_count; ii++)
        {
            Mel_Scratch_Image* si = &block->images[ii];
            if (si->image.width == desc.width &&
                si->image.height == desc.height &&
                si->image.format == desc.format)
            {
                block->active_count++;

                si->image.subresource_states[0] = (Mel_Gpu_Image_State){
                    .layout = MEL_GPU_IMAGE_LAYOUT_UNDEFINED,
                    .stage = MEL_GPU_STAGE_NONE,
                    .access = MEL_GPU_ACCESS_NONE,
                };

                return si->image;
            }
        }

        VkDeviceSize needed = mel__scratch_image_size(pool->dev, desc);
        if (needed <= block->size)
        {
            if (block->image_count >= block->image_capacity)
            {
                u32 new_cap = block->image_capacity * 2;
                Mel_Scratch_Image* new_images = mel_alloc_array(pool->alloc, Mel_Scratch_Image, new_cap);
                memcpy(new_images, block->images, block->image_count * sizeof(Mel_Scratch_Image));
                mel_dealloc(pool->alloc, block->images);
                block->images = new_images;
                block->image_capacity = new_cap;
            }

            Mel_Scratch_Image si = mel__scratch_image_create(pool->dev, block, desc, pool->alloc);
            block->images[block->image_count++] = si;
            block->active_count++;
            return si.image;
        }
    }

    VkDeviceSize needed = mel__scratch_image_size(pool->dev, desc);
    VkDeviceSize block_size = needed;
    if (block_size < 64 * 1024 * 1024)
        block_size = 64 * 1024 * 1024;

    VkImageUsageFlags probe_usage = desc.usage
        ? mel__gpu_image_usage_to_vk(desc.usage)
        : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    VkImageCreateInfo probe_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = mel__gpu_format_to_vk(desc.format),
        .extent = { desc.width, desc.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = probe_usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage probe;
    VkResult r = vkCreateImage(mel__gpu_device_vk(pool->dev)->device, &probe_info, nullptr, &probe);
    assert(r == VK_SUCCESS);
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(mel__gpu_device_vk(pool->dev)->device, probe, &reqs);
    vkDestroyImage(mel__gpu_device_vk(pool->dev)->device, probe, nullptr);

    if (block_size < reqs.size)
        block_size = reqs.size;

    u32 mem_type = mel__scratch_find_memory_type(pool->dev, reqs.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (pool->block_count >= pool->block_capacity)
    {
        u32 new_cap = pool->block_capacity * 2;
        Mel_Scratch_Block* new_blocks = mel_alloc_array(pool->alloc, Mel_Scratch_Block, new_cap);
        memcpy(new_blocks, pool->blocks, pool->block_count * sizeof(Mel_Scratch_Block));
        mel_dealloc(pool->alloc, pool->blocks);
        pool->blocks = new_blocks;
        pool->block_capacity = new_cap;
    }

    Mel_Scratch_Block block = mel__scratch_block_create(pool->dev, block_size, mem_type, pool->alloc);
    Mel_Scratch_Image si = mel__scratch_image_create(pool->dev, &block, desc, pool->alloc);
    block.images[block.image_count++] = si;
    block.active_count++;

    pool->blocks[pool->block_count++] = block;
    return si.image;
}

void mel_scratch_release(Mel_Scratch_Pool* pool, Mel_Gpu_Image img)
{
    assert(pool != nullptr);

    for (u32 bi = 0; bi < pool->block_count; bi++)
    {
        Mel_Scratch_Block* block = &pool->blocks[bi];
        for (u32 ii = 0; ii < block->image_count; ii++)
        {
            if (block->images[ii].image._handle == img._handle)
            {
                assert(block->active_count > 0);
                block->active_count--;
                return;
            }
        }
    }

    assert(false && "image not found in scratch pool");
}

void mel_scratch_pool_reset(Mel_Scratch_Pool* pool)
{
    assert(pool != nullptr);

    for (u32 bi = 0; bi < pool->block_count; bi++)
        pool->blocks[bi].active_count = 0;
}
