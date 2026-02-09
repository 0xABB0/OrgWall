#define VK_NO_PROTOTYPES
#include "gpu.descriptor.h"

static VkDescriptorType descriptor_type_to_vk(u32 type)
{
    switch (type)
    {
        case MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case MEL_GPU_DESCRIPTOR_STORAGE_BUFFER:        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE:         return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case MEL_GPU_DESCRIPTOR_STORAGE_IMAGE:         return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case MEL_GPU_DESCRIPTOR_SAMPLER:               return VK_DESCRIPTOR_TYPE_SAMPLER;
        case MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default: assert(false && "Unknown descriptor type"); return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

void mel_gpu_descriptor_layout_init_opt(Mel_Gpu_Descriptor_Layout* dl, Mel_Gpu_Device* dev, Mel_Gpu_Descriptor_Layout_Opt opt)
{
    assert(dl != nullptr);
    assert(dev != nullptr);
    assert(opt.bindings != nullptr);
    assert(opt.binding_count > 0);

    VkDescriptorSetLayoutBinding vk_bindings[16];
    assert(opt.binding_count <= 16);

    for (u32 i = 0; i < opt.binding_count; i++)
    {
        vk_bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = opt.bindings[i].binding,
            .descriptorType = descriptor_type_to_vk(opt.bindings[i].type),
            .descriptorCount = opt.bindings[i].count > 0 ? opt.bindings[i].count : 1,
            .stageFlags = opt.bindings[i].stages,
        };
    }

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = opt.binding_count,
        .pBindings = vk_bindings,
    };

    VkResult r = vkCreateDescriptorSetLayout(dev->device, &layout_info, nullptr, &dl->layout);
    assert(r == VK_SUCCESS);

    dl->binding_count = opt.binding_count;
}

void mel_gpu_descriptor_layout_shutdown(Mel_Gpu_Descriptor_Layout* dl, Mel_Gpu_Device* dev)
{
    assert(dl != nullptr);
    assert(dev != nullptr);

    if (dl->layout)
    {
        vkDestroyDescriptorSetLayout(dev->device, dl->layout, nullptr);
        dl->layout = VK_NULL_HANDLE;
    }
}

void mel_gpu_descriptor_pool_init_opt(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev, Mel_Gpu_Descriptor_Pool_Opt opt)
{
    assert(dp != nullptr);
    assert(dev != nullptr);
    assert(opt.layout != nullptr);
    assert(opt.max_sets > 0);

    dp->layout = opt.layout->layout;
    dp->max_sets = opt.max_sets;

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = opt.max_sets,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = opt.max_sets,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    VkResult r = vkCreateDescriptorPool(dev->device, &pool_info, nullptr, &dp->pool);
    assert(r == VK_SUCCESS);
}

void mel_gpu_descriptor_pool_shutdown(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev)
{
    assert(dp != nullptr);
    assert(dev != nullptr);

    if (dp->pool)
    {
        vkDestroyDescriptorPool(dev->device, dp->pool, nullptr);
        dp->pool = VK_NULL_HANDLE;
    }
}

VkDescriptorSet mel_gpu_descriptor_pool_alloc(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev)
{
    assert(dp != nullptr);
    assert(dev != nullptr);
    assert(dp->pool != VK_NULL_HANDLE);
    assert(dp->layout != VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dp->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &dp->layout,
    };

    VkDescriptorSet set;
    VkResult r = vkAllocateDescriptorSets(dev->device, &alloc_info, &set);
    assert(r == VK_SUCCESS);

    return set;
}

void mel_gpu_descriptor_write_texture(Mel_Gpu_Device* dev, VkDescriptorSet set,
                                      u32 binding, VkImageView view, VkSampler sampler)
{
    assert(dev != nullptr);
    assert(set != VK_NULL_HANDLE);

    VkDescriptorImageInfo image_info = {
        .sampler = sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };

    vkUpdateDescriptorSets(dev->device, 1, &write, 0, nullptr);
}

void mel_gpu_descriptor_write_buffer(Mel_Gpu_Device* dev, VkDescriptorSet set,
                                     u32 binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range,
                                     VkDescriptorType type)
{
    assert(dev != nullptr);
    assert(set != VK_NULL_HANDLE);
    assert(buffer != VK_NULL_HANDLE);

    VkDescriptorBufferInfo buffer_info = {
        .buffer = buffer,
        .offset = offset,
        .range = range,
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = type,
        .pBufferInfo = &buffer_info,
    };

    vkUpdateDescriptorSets(dev->device, 1, &write, 0, nullptr);
}

void mel_gpu_descriptor_bind(VkCommandBuffer cmd, VkPipelineLayout layout, VkDescriptorSet set)
{
    assert(cmd != VK_NULL_HANDLE);
    assert(layout != VK_NULL_HANDLE);
    assert(set != VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
}
