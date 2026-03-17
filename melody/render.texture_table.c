#include "render.texture_table.h"
#include "gpu.device.h"
#include "gpu.cmd.h"
#include "gpu.types.vulkan.h"

void mel_texture_table_init_opt(Mel_Texture_Table* tt, Mel_Gpu_Device* dev, const Mel_Alloc* alloc, Mel_Texture_Table_Opt opt)
{
    assert(tt != nullptr);
    assert(dev != nullptr);
    assert(alloc != nullptr);
    assert(dev->capabilities.descriptor_indexing);
    assert(opt.capacity > 0);

    *tt = (Mel_Texture_Table){0};
    tt->dev = dev;
    tt->capacity = opt.capacity;

    Mel_Gpu_Descriptor_Binding binding = {
        .binding = 0,
        .type = MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
        .count = opt.capacity,
        .stages = MEL_GPU_SHADER_STAGE_FRAGMENT | MEL_GPU_SHADER_STAGE_VERTEX,
        .flags = MEL_GPU_DESCRIPTOR_BINDING_PARTIALLY_BOUND
               | MEL_GPU_DESCRIPTOR_BINDING_VARIABLE_COUNT
               | MEL_GPU_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND,
    };

    mel_gpu_descriptor_layout_init(&tt->layout, dev,
        .bindings = &binding,
        .binding_count = 1);

    mel_gpu_descriptor_pool_init(&tt->pool, dev,
        .layout = &tt->layout,
        .max_sets = 1,
        .update_after_bind = true,
        .variable_count = opt.capacity);

    tt->_set = mel_gpu_descriptor_pool_alloc(&tt->pool, dev);

    mel_bitset_init(&tt->used, opt.capacity, alloc);
}

void mel_texture_table_shutdown(Mel_Texture_Table* tt)
{
    assert(tt != nullptr);

    mel_bitset_free(&tt->used);
    mel_gpu_descriptor_pool_shutdown(&tt->pool, tt->dev);
    mel_gpu_descriptor_layout_shutdown(&tt->layout, tt->dev);

    *tt = (Mel_Texture_Table){0};
}

u32 mel_texture_table_add(Mel_Texture_Table* tt, void* view, void* sampler)
{
    assert(tt != nullptr);
    assert(view != nullptr);
    assert(sampler != nullptr);

    usize slot = mel_bitset_first_clear(&tt->used);
    assert(slot < tt->capacity);

    mel_bitset_set(&tt->used, slot);

    VkDescriptorImageInfo image_info = {
        .sampler = (VkSampler)sampler,
        .imageView = (VkImageView)view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = (VkDescriptorSet)tt->_set,
        .dstBinding = 0,
        .dstArrayElement = (u32)slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };

    vkUpdateDescriptorSets(tt->dev->device, 1, &write, 0, nullptr);

    return (u32)slot;
}

void mel_texture_table_remove(Mel_Texture_Table* tt, u32 index)
{
    assert(tt != nullptr);
    assert(index < tt->capacity);
    assert(mel_bitset_get(&tt->used, index));

    mel_bitset_clear_bit(&tt->used, index);
}

void mel_texture_table_bind(Mel_Texture_Table* tt, Mel_Gpu_Cmd* cmd, void* pipeline_layout, u32 set_index)
{
    assert(tt != nullptr);
    assert(cmd != nullptr);
    assert(pipeline_layout != nullptr);

    VkDescriptorSet vk_set = (VkDescriptorSet)tt->_set;
    vkCmdBindDescriptorSets((VkCommandBuffer)cmd->_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        (VkPipelineLayout)pipeline_layout, set_index, 1, &vk_set, 0, nullptr);
}

u32 mel_texture_table_count(Mel_Texture_Table* tt)
{
    assert(tt != nullptr);
    return (u32)mel_bitset_count_set(&tt->used);
}
