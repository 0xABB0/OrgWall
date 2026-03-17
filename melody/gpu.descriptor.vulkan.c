#include "gpu.descriptor.h"
#include "gpu.device.vulkan.h"
#include "gpu.cmd.h"
#include "gpu.buffer.h"
#include "gpu.types.vulkan.h"

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
    VkDescriptorBindingFlags vk_binding_flags[16];
    assert(opt.binding_count <= 16);

    bool needs_binding_flags = false;
    for (u32 i = 0; i < opt.binding_count; i++)
    {
        vk_bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = opt.bindings[i].binding,
            .descriptorType = descriptor_type_to_vk(opt.bindings[i].type),
            .descriptorCount = opt.bindings[i].count > 0 ? opt.bindings[i].count : 1,
            .stageFlags = mel__gpu_shader_stage_to_vk(opt.bindings[i].stages),
        };

        VkDescriptorBindingFlags flags = 0;
        if (opt.bindings[i].flags & MEL_GPU_DESCRIPTOR_BINDING_PARTIALLY_BOUND)
            flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        if (opt.bindings[i].flags & MEL_GPU_DESCRIPTOR_BINDING_VARIABLE_COUNT)
        {
            Mel_Gpu_Device_Vulkan* vk = mel__gpu_device_vk(dev);
            if (!vk->has_portability_subset)
                flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        }
        if (opt.bindings[i].flags & MEL_GPU_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND)
            flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        vk_binding_flags[i] = flags;
        if (flags)
            needs_binding_flags = true;
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = opt.binding_count,
        .pBindingFlags = vk_binding_flags,
    };

    VkDescriptorSetLayoutCreateFlags layout_flags = 0;
    for (u32 i = 0; i < opt.binding_count; i++)
    {
        if (opt.bindings[i].flags & MEL_GPU_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND)
        {
            layout_flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            break;
        }
    }

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = needs_binding_flags ? &binding_flags_info : nullptr,
        .flags = layout_flags,
        .bindingCount = opt.binding_count,
        .pBindings = vk_bindings,
    };

    VkDescriptorSetLayout vk_layout = VK_NULL_HANDLE;
    VkResult r = vkCreateDescriptorSetLayout(mel__gpu_device_vk(dev)->device, &layout_info, nullptr, &vk_layout);
    assert(r == VK_SUCCESS);
    dl->_layout = vk_layout;

    dl->binding_count = opt.binding_count;
    for (u32 i = 0; i < opt.binding_count; i++)
        dl->_bindings[i] = opt.bindings[i];
}

void mel_gpu_descriptor_layout_shutdown(Mel_Gpu_Descriptor_Layout* dl, Mel_Gpu_Device* dev)
{
    assert(dl != nullptr);
    assert(dev != nullptr);

    if (dl->_layout)
    {
        vkDestroyDescriptorSetLayout(mel__gpu_device_vk(dev)->device, (VkDescriptorSetLayout)dl->_layout, nullptr);
        dl->_layout = nullptr;
    }
}

void mel_gpu_descriptor_pool_init_opt(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev, Mel_Gpu_Descriptor_Pool_Opt opt)
{
    assert(dp != nullptr);
    assert(dev != nullptr);
    assert(opt.layout != nullptr);
    assert(opt.max_sets > 0);

    dp->_layout = opt.layout->_layout;
    dp->max_sets = opt.max_sets;
    dp->variable_count = opt.variable_count;

    VkDescriptorPoolSize pool_sizes[16];
    u32 pool_size_count = 0;

    for (u32 i = 0; i < opt.layout->binding_count; i++)
    {
        VkDescriptorType vk_type = descriptor_type_to_vk(opt.layout->_bindings[i].type);
        u32 count = opt.layout->_bindings[i].count > 0 ? opt.layout->_bindings[i].count : 1;

        bool merged = false;
        for (u32 j = 0; j < pool_size_count; j++)
        {
            if (pool_sizes[j].type == vk_type)
            {
                pool_sizes[j].descriptorCount += count * opt.max_sets;
                merged = true;
                break;
            }
        }
        if (!merged)
        {
            assert(pool_size_count < 16);
            pool_sizes[pool_size_count++] = (VkDescriptorPoolSize){
                .type = vk_type,
                .descriptorCount = count * opt.max_sets,
            };
        }
    }

    VkDescriptorPoolCreateFlags pool_flags = 0;
    if (opt.update_after_bind)
        pool_flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = pool_flags,
        .maxSets = opt.max_sets,
        .poolSizeCount = pool_size_count,
        .pPoolSizes = pool_sizes,
    };

    VkDescriptorPool vk_pool = VK_NULL_HANDLE;
    VkResult r = vkCreateDescriptorPool(mel__gpu_device_vk(dev)->device, &pool_info, nullptr, &vk_pool);
    assert(r == VK_SUCCESS);
    dp->_pool = vk_pool;
}

void mel_gpu_descriptor_pool_shutdown(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev)
{
    assert(dp != nullptr);
    assert(dev != nullptr);

    if (dp->_pool)
    {
        vkDestroyDescriptorPool(mel__gpu_device_vk(dev)->device, (VkDescriptorPool)dp->_pool, nullptr);
        dp->_pool = nullptr;
    }
}

void* mel_gpu_descriptor_pool_alloc(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev)
{
    assert(dp != nullptr);
    assert(dev != nullptr);
    assert(dp->_pool != nullptr);
    assert(dp->_layout != nullptr);

    VkDescriptorSetLayout vk_layout = (VkDescriptorSetLayout)dp->_layout;
    VkDescriptorPool vk_pool = (VkDescriptorPool)dp->_pool;

    u32 variable_count = dp->variable_count;
    bool use_variable_count = variable_count > 0 && !mel__gpu_device_vk(dev)->has_portability_subset;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &variable_count,
    };

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = use_variable_count ? &variable_info : nullptr,
        .descriptorPool = vk_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk_layout,
    };

    VkDescriptorSet set;
    VkResult r = vkAllocateDescriptorSets(mel__gpu_device_vk(dev)->device, &alloc_info, &set);
    assert(r == VK_SUCCESS);

    return set;
}

void mel_gpu_descriptor_write_texture(Mel_Gpu_Device* dev, void* set,
                                      u32 binding, void* view, void* sampler)
{
    assert(dev != nullptr);
    assert(set != nullptr);

    VkDescriptorImageInfo image_info = {
        .sampler = (VkSampler)sampler,
        .imageView = (VkImageView)view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = (VkDescriptorSet)set,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };

    vkUpdateDescriptorSets(mel__gpu_device_vk(dev)->device, 1, &write, 0, nullptr);
}

void mel_gpu_descriptor_write_buffer(Mel_Gpu_Device* dev, void* set,
                                     u32 binding, Mel_Gpu_Buffer* buffer, u64 offset, u64 range,
                                     u32 type)
{
    assert(dev != nullptr);
    assert(set != nullptr);
    assert(buffer != nullptr);
    assert(buffer->_handle != nullptr);

    VkDescriptorBufferInfo buffer_info = {
        .buffer = (VkBuffer)buffer->_handle,
        .offset = offset,
        .range = range,
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = (VkDescriptorSet)set,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = descriptor_type_to_vk(type),
        .pBufferInfo = &buffer_info,
    };

    vkUpdateDescriptorSets(mel__gpu_device_vk(dev)->device, 1, &write, 0, nullptr);
}

void mel_gpu_descriptor_bind(Mel_Gpu_Cmd* cmd, void* pipeline_layout, void* set)
{
    assert(cmd != nullptr);
    assert(cmd->_cmd != nullptr);
    assert(pipeline_layout != nullptr);
    assert(set != nullptr);

    VkDescriptorSet vk_set = (VkDescriptorSet)set;
    vkCmdBindDescriptorSets((VkCommandBuffer)cmd->_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        (VkPipelineLayout)pipeline_layout, 0, 1, &vk_set, 0, nullptr);
}
