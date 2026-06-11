#include "vk_backend.h"

#include <log/log.h>

static VkDescriptorType mel_gpu__descriptor_type(Mel_Gpu_Descriptor_Kind kind)
{
    switch (kind)
    {
    case MEL_GPU_DESCRIPTOR_SAMPLER:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case MEL_GPU_DESCRIPTOR_STORAGE_IMAGE:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case MEL_GPU_DESCRIPTOR_STORAGE_BUFFER:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    return VK_DESCRIPTOR_TYPE_SAMPLER;
}

static VkDescriptorPool mel_gpu__classic_pool_new(Mel_Gpu_Device* dev)
{
    const u32            PER_TYPE = 256, MAX_SETS = 256;
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, PER_TYPE },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, PER_TYPE },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PER_TYPE },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, PER_TYPE },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, PER_TYPE },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PER_TYPE },
    };
    VkDescriptorPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = MAX_SETS,
        .poolSizeCount = (u32)(sizeof sizes / sizeof sizes[0]),
        .pPoolSizes = sizes,
    };
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(dev->vk, &pci, NULL, &pool) != VK_SUCCESS)
    {
        mel_log_error("gpu", "bind_group: vkCreateDescriptorPool (classic) failed");
        return VK_NULL_HANDLE;
    }
    if (dev->classic_pool_count == dev->classic_pool_cap)
    {
        u32 cap = dev->classic_pool_cap ? dev->classic_pool_cap * 2 : 4;
        dev->classic_pools = dev->classic_pools ? mel_realloc(dev->alloc, dev->classic_pools, sizeof(VkDescriptorPool) * cap) : mel_alloc(dev->alloc, sizeof(VkDescriptorPool) * cap);
        dev->classic_pool_cap = cap;
    }
    dev->classic_pools[dev->classic_pool_count++] = pool;
    return pool;
}

VkDescriptorSet mel_gpu__classic_descriptor_alloc(Mel_Gpu_Device* dev, VkDescriptorSetLayout layout, VkDescriptorPool* out_pool)
{
    mel_mutex_lock(&dev->classic_pool_lock);
    VkDescriptorSet set = VK_NULL_HANDLE;
    for (u32 attempt = 0; attempt < 2 && set == VK_NULL_HANDLE; attempt++)
    {
        VkDescriptorPool pool = dev->classic_pool_count ? dev->classic_pools[dev->classic_pool_count - 1] : mel_gpu__classic_pool_new(dev);
        if (!pool)
            break;
        VkDescriptorSetAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &layout };
        VkResult                    r = vkAllocateDescriptorSets(dev->vk, &ai, &set);
        if (r == VK_SUCCESS)
        {
            *out_pool = pool;
            break;
        }
        set = VK_NULL_HANDLE;
        if (r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL)
            mel_gpu__classic_pool_new(dev);
        else
        {
            mel_log_error("gpu", "bind_group: vkAllocateDescriptorSets failed: %s", mel_gpu__vk_result_str(r));
            break;
        }
    }
    mel_mutex_unlock(&dev->classic_pool_lock);
    return set;
}

void mel_gpu__classic_pools_shutdown(Mel_Gpu_Device* dev)
{
    for (u32 i = 0; i < dev->classic_pool_count; i++)
        if (dev->classic_pools[i])
            vkDestroyDescriptorPool(dev->vk, dev->classic_pools[i], NULL);
    if (dev->classic_pools)
        mel_dealloc(dev->alloc, dev->classic_pools);
    dev->classic_pools = NULL;
    dev->classic_pool_count = dev->classic_pool_cap = 0;
}

Mel_Gpu_Bind_Group_Layout mel_gpu_bind_group_layout_create(Mel_Gpu_Device* dev, const Mel_Gpu_Bind_Group_Layout_Entry* entries, u32 count)
{
    Mel_Gpu_Bind_Group_Layout h = { mel_gpu_handle_null() };
    if (!dev || (!entries && count))
    {
        mel_assert(!"bind_group_layout_create: null entries");
        return h;
    }

    VkDescriptorSetLayoutBinding* binds = count ? mel_alloc_array(dev->alloc, VkDescriptorSetLayoutBinding, count) : NULL;
    for (u32 i = 0; i < count; i++)
        binds[i] = (VkDescriptorSetLayoutBinding){
            .binding = entries[i].binding,
            .descriptorType = mel_gpu__descriptor_type(entries[i].kind),
            .descriptorCount = entries[i].count ? entries[i].count : 1u,
            .stageFlags = VK_SHADER_STAGE_ALL,
        };

    VkDescriptorSetLayoutCreateInfo lci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = count, .pBindings = binds };
    VkDescriptorSetLayout           layout = VK_NULL_HANDLE;
    VkResult                        r = vkCreateDescriptorSetLayout(dev->vk, &lci, NULL, &layout);
    if (binds)
        mel_dealloc(dev->alloc, binds);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "bind_group_layout_create: vkCreateDescriptorSetLayout failed: %s", mel_gpu__vk_result_str(r));
        return h;
    }

    Mel_Gpu_Bind_Group_Layout_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.layout = layout;
    obj.entry_count = count;
    if (count)
    {
        obj.entries = mel_alloc_array(dev->alloc, Mel_Gpu_Bind_Group_Layout_Entry, count);
        for (u32 i = 0; i < count; i++)
            obj.entries[i] = entries[i];
    }
    h.slot = mel_gpu__table_insert(dev, &dev->bind_group_layouts, &obj);
    return h;
}

void mel_gpu_bind_group_layout_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout)
{
    const void* trk = mel_gpu__track_key(&dev->bind_group_layouts, layout.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    Mel_Gpu_Bind_Group_Layout_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_group_layouts, layout.slot, &o))
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    VkDescriptorSetLayout            vk = o.layout;
    Mel_Gpu_Bind_Group_Layout_Entry* entries = o.entries;
    mel_gpu__table_remove(dev, &dev->bind_group_layouts, layout.slot);
    if (vk)
        vkDestroyDescriptorSetLayout(dev->vk, vk, NULL);
    if (entries)
        mel_dealloc(dev->alloc, entries);
    mel_gpu__track_exit(dev, trk);
}

bool mel_gpu_bind_group_layout_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout) { return mel_gpu__table_alive(dev, &dev->bind_group_layouts, layout.slot); }

bool mel_gpu__bind_group_layout_vk(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout, VkDescriptorSetLayout* out)
{
    Mel_Gpu_Bind_Group_Layout_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_group_layouts, layout.slot, &o))
        return false;
    *out = o.layout;
    return true;
}

Mel_Gpu_Bind_Group mel_gpu_bind_group_create(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout)
{
    Mel_Gpu_Bind_Group h = { mel_gpu_handle_null() };
    Mel_Gpu_Bind_Group_Layout_Obj lo;
    if (!dev || !mel_gpu__table_get_copy(dev, &dev->bind_group_layouts, layout.slot, &lo))
    {
        mel_assert(!"bind_group_create: invalid layout handle");
        return h;
    }
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet  set = mel_gpu__classic_descriptor_alloc(dev, lo.layout, &pool);
    if (!set)
        return h;

    Mel_Gpu_Bind_Group_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.set = set;
    obj.pool = pool;
    obj.layout = layout.slot;
    h.slot = mel_gpu__table_insert(dev, &dev->bind_groups, &obj);
    return h;
}

void mel_gpu_bind_group_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group)
{
    const void* trk = mel_gpu__track_key(&dev->bind_groups, group.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    Mel_Gpu_Bind_Group_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_groups, group.slot, &o))
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    VkDescriptorSet  set = o.set;
    VkDescriptorPool pool = o.pool;
    mel_gpu__table_remove(dev, &dev->bind_groups, group.slot);
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .descriptor_set = set, .descriptor_set_pool = pool });
    mel_gpu__track_exit(dev, trk);
}

bool mel_gpu_bind_group_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group) { return mel_gpu__table_alive(dev, &dev->bind_groups, group.slot); }

bool mel_gpu__bind_group_set(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, VkDescriptorSet* out)
{
    Mel_Gpu_Bind_Group_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_groups, group.slot, &o))
        return false;
    *out = o.set;
    return true;
}

static bool mel_gpu__bg_kind(Mel_Gpu_Device* dev, const Mel_Gpu_Bind_Group_Obj* g, u32 binding, Mel_Gpu_Descriptor_Kind* out_kind, VkDescriptorSet* out_set)
{
    Mel_Gpu_Bind_Group_Layout_Obj lo;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_group_layouts, g->layout, &lo))
    {
        mel_assert(!"bind_group write: source layout was destroyed before the group");
        return false;
    }
    for (u32 i = 0; i < lo.entry_count; i++)
        if (lo.entries[i].binding == binding)
        {
            *out_kind = lo.entries[i].kind;
            *out_set = g->set;
            return true;
        }
    mel_log_error("gpu", "bind_group write: binding %u is not declared by the group's layout", binding);
    mel_assert(!"bind_group write: undeclared binding");
    return false;
}

static void mel_gpu__bg_write_image(Mel_Gpu_Device* dev, VkDescriptorSet set, u32 binding, u32 elem, VkDescriptorType type, VkImageView view, VkSampler sampler, VkImageLayout layout)
{
    VkDescriptorImageInfo ii = { .sampler = sampler, .imageView = view, .imageLayout = layout };
    VkWriteDescriptorSet  w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding,
        .dstArrayElement = elem,
        .descriptorCount = 1,
        .descriptorType = type,
        .pImageInfo = &ii,
    };
    vkUpdateDescriptorSets(dev->vk, 1, &w, 0, NULL);
}

void mel_gpu_bind_group_write_texture(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view)
{
    Mel_Gpu_Bind_Group_Obj   g;
    Mel_Gpu_Texture_View_Obj vo;
    Mel_Gpu_Descriptor_Kind  kind;
    VkDescriptorSet          set;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_groups, group.slot, &g) || !mel_gpu__texture_view_get(dev, view, &vo) || !mel_gpu__bg_kind(dev, &g, binding, &kind, &set))
        return;
    VkImageLayout layout = kind == MEL_GPU_DESCRIPTOR_STORAGE_IMAGE ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    mel_gpu__bg_write_image(dev, set, binding, array_element, mel_gpu__descriptor_type(kind), vo.view, VK_NULL_HANDLE, layout);
}

void mel_gpu_bind_group_write_sampler(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Sampler sampler)
{
    Mel_Gpu_Bind_Group_Obj  g;
    VkSampler               vk = VK_NULL_HANDLE;
    Mel_Gpu_Descriptor_Kind kind;
    VkDescriptorSet         set;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_groups, group.slot, &g) || !mel_gpu__sampler_get(dev, sampler, &vk) || !mel_gpu__bg_kind(dev, &g, binding, &kind, &set))
        return;
    mel_gpu__bg_write_image(dev, set, binding, array_element, VK_DESCRIPTOR_TYPE_SAMPLER, VK_NULL_HANDLE, vk, VK_IMAGE_LAYOUT_UNDEFINED);
}

void mel_gpu_bind_group_write_combined(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view, Mel_Gpu_Sampler sampler)
{
    Mel_Gpu_Bind_Group_Obj   g;
    Mel_Gpu_Texture_View_Obj vo;
    VkSampler                vk = VK_NULL_HANDLE;
    Mel_Gpu_Descriptor_Kind  kind;
    VkDescriptorSet          set;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_groups, group.slot, &g) || !mel_gpu__texture_view_get(dev, view, &vo) || !mel_gpu__sampler_get(dev, sampler, &vk) || !mel_gpu__bg_kind(dev, &g, binding, &kind, &set))
        return;
    mel_gpu__bg_write_image(dev, set, binding, array_element, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, vo.view, vk, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void mel_gpu_bind_group_write_buffer(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Buffer buffer)
{
    Mel_Gpu_Bind_Group_Obj  g;
    VkBuffer                vk = VK_NULL_HANDLE;
    Mel_Gpu_Descriptor_Kind kind;
    VkDescriptorSet         set;
    if (!mel_gpu__table_get_copy(dev, &dev->bind_groups, group.slot, &g) || !mel_gpu__buffer_get(dev, buffer, &vk) || !mel_gpu__bg_kind(dev, &g, binding, &kind, &set))
        return;
    VkDescriptorBufferInfo bi = { .buffer = vk, .offset = 0, .range = VK_WHOLE_SIZE };
    VkWriteDescriptorSet   w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding,
        .dstArrayElement = array_element,
        .descriptorCount = 1,
        .descriptorType = mel_gpu__descriptor_type(kind),
        .pBufferInfo = &bi,
    };
    vkUpdateDescriptorSets(dev->vk, 1, &w, 0, NULL);
}

void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Command_List* cmd, u32 set_index, Mel_Gpu_Bind_Group group)
{
    mel_assert(cmd);
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!mel_gpu__bind_group_set(cmd->dev, group, &set))
    {
        mel_assert(!"cmd_bind_descriptor_set: invalid bind group");
        return;
    }
    mel_assert(cmd->cur_layout != VK_NULL_HANDLE && "cmd_bind_descriptor_set: bind a pipeline first");
    vkCmdBindDescriptorSets(cmd->cb, cmd->cur_bind_point, cmd->cur_layout, set_index, 1, &set, 0, NULL);
}
