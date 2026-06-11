#include "vk_backend.h"

#include <log/log.h>

static u32 mel_gpu__min_u32(u32 a, u32 b) { return a < b ? a : b; }
static u32 mel_gpu__max_u32(u32 a, u32 b) { return a > b ? a : b; }

static const VkDescriptorType mel_gpu__class_desc_type[MEL_GPU_BINDLESS_CLASS_COUNT] = {
    [MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE] = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    [MEL_GPU_BINDLESS_CLASS_SAMPLER] = VK_DESCRIPTOR_TYPE_SAMPLER,
    [MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    [MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    [MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
};

static const char* mel_gpu__class_name[MEL_GPU_BINDLESS_CLASS_COUNT] = {
    [MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE] = "sampled-image",
    [MEL_GPU_BINDLESS_CLASS_SAMPLER] = "sampler",
    [MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER] = "storage-buffer",
    [MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER] = "uniform-buffer",
    [MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE] = "storage-image",
};

static bool mel_gpu__class_is_image(u32 cls)
{
    return cls == MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE || cls == MEL_GPU_BINDLESS_CLASS_SAMPLER || cls == MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE;
}

u32 mel_gpu__heap_cap_for_class(Mel_Gpu_Device* dev, u32 binding_class)
{
    if (binding_class >= MEL_GPU_BINDLESS_CLASS_COUNT)
        return 0;
    Mel_Gpu_Bindless* b = &dev->bindless;
    return b->growable ? b->hw_max[binding_class] : b->caps[binding_class];
}

static bool mel_gpu__class_layout_create(Mel_Gpu_Device* dev, u32 cls, u32 count, VkDescriptorSetLayout* out_layout)
{
    Mel_Gpu_Bindless*            b = &dev->bindless;
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = mel_gpu__class_desc_type[cls],
        .descriptorCount = count,
        .stageFlags = VK_SHADER_STAGE_ALL,
    };
    VkDescriptorBindingFlags flag = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    if (b->growable)
        flag |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 1,
        .pBindingFlags = &flag,
    };
    VkDescriptorSetLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flags_ci,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(dev->vk, &lci, NULL, out_layout) != VK_SUCCESS)
    {
        mel_log_error("gpu", "bindless: vkCreateDescriptorSetLayout failed for %s class (%u descriptors)", mel_gpu__class_name[cls], count);
        *out_layout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static Mel_Gpu_Bindless_Epoch* mel_gpu__epoch_try(Mel_Gpu_Device* dev, u32 cls, u32 cap, bool quiet)
{
    Mel_Gpu_Bindless* b = &dev->bindless;

    VkDescriptorPoolSize       size = { mel_gpu__class_desc_type[cls], cap };
    VkDescriptorPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &size,
    };
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(dev->vk, &pci, NULL, &pool) != VK_SUCCESS)
    {
        if (!quiet)
            mel_log_error("gpu", "bindless: vkCreateDescriptorPool failed for %s class (%u descriptors)", mel_gpu__class_name[cls], cap);
        return NULL;
    }

    VkDescriptorSetVariableDescriptorCountAllocateInfo var_ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &cap,
    };
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = b->growable ? &var_ai : NULL,
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &b->set_layouts[cls],
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(dev->vk, &ai, &set) != VK_SUCCESS)
    {
        if (!quiet)
            mel_log_error("gpu", "bindless: vkAllocateDescriptorSets failed for %s class (%u descriptors)", mel_gpu__class_name[cls], cap);
        vkDestroyDescriptorPool(dev->vk, pool, NULL);
        return NULL;
    }

    Mel_Gpu_Bindless_Epoch* e = mel_alloc_type(dev->alloc, Mel_Gpu_Bindless_Epoch);
    *e = (Mel_Gpu_Bindless_Epoch){ .cls = cls, .pool = pool, .set = set, .cap = cap };
    return e;
}

static void mel_gpu__epoch_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bindless_Epoch* e)
{
    vkDestroyDescriptorPool(dev->vk, e->pool, NULL);
    mel_dealloc(dev->alloc, e);
}

void mel_gpu__bindless_init(Mel_Gpu_Device* dev, bool want)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    *b = (Mel_Gpu_Bindless){ 0 };

    if (!want || dev->caps.memory.bindless.tier == MEL_GPU_TIER_NONE)
        return;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev->phys, &props);
    if (props.limits.maxBoundDescriptorSets < MEL_GPU_BINDLESS_CLASS_COUNT)
    {
        mel_log_error("gpu", "bindless: device binds at most %u descriptor sets but the per-class heap needs %u; heap disabled", props.limits.maxBoundDescriptorSets, (u32)MEL_GPU_BINDLESS_CLASS_COUNT);
        return;
    }

    b->growable = dev->caps.memory.bindless.growable;

    b->hw_max[MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE] = dev->caps.memory.bindless.max_texture_view_slots;
    b->hw_max[MEL_GPU_BINDLESS_CLASS_SAMPLER] = dev->caps.memory.bindless.max_sampler_slots;
    b->hw_max[MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER] = dev->caps.memory.bindless.max_storage_buffer_slots;
    b->hw_max[MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER] = dev->caps.memory.bindless.max_uniform_buffer_slots;
    b->hw_max[MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE] = dev->caps.memory.bindless.max_storage_image_slots;

    const u32 SEED_IMAGE = 1024, SEED_SAMPLER = 256, SEED_BUFFER = 1024;
    const u32 FIXED_IMAGE = 16384, FIXED_SAMPLER = 2048, FIXED_BUFFER = 16384;
    if (b->growable)
    {
        b->caps[MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE] = mel_gpu__min_u32(SEED_IMAGE, b->hw_max[MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE]);
        b->caps[MEL_GPU_BINDLESS_CLASS_SAMPLER] = mel_gpu__min_u32(SEED_SAMPLER, b->hw_max[MEL_GPU_BINDLESS_CLASS_SAMPLER]);
        b->caps[MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER] = mel_gpu__min_u32(SEED_BUFFER, b->hw_max[MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER]);
        b->caps[MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER] = mel_gpu__min_u32(SEED_BUFFER, b->hw_max[MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER]);
        b->caps[MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE] = mel_gpu__min_u32(SEED_IMAGE, b->hw_max[MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE]);
    }
    else
    {
        b->caps[MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE] = mel_gpu__min_u32(FIXED_IMAGE, b->hw_max[MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE]);
        b->caps[MEL_GPU_BINDLESS_CLASS_SAMPLER] = mel_gpu__min_u32(FIXED_SAMPLER, b->hw_max[MEL_GPU_BINDLESS_CLASS_SAMPLER]);
        b->caps[MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER] = mel_gpu__min_u32(FIXED_BUFFER, b->hw_max[MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER]);
        b->caps[MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER] = mel_gpu__min_u32(FIXED_BUFFER, b->hw_max[MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER]);
        b->caps[MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE] = mel_gpu__min_u32(FIXED_IMAGE, b->hw_max[MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE]);
        for (u32 cls = 0; cls < MEL_GPU_BINDLESS_CLASS_COUNT; cls++)
            b->hw_max[cls] = b->caps[cls];
    }

    for (u32 cls = 0; cls < MEL_GPU_BINDLESS_CLASS_COUNT; cls++)
    {
        while (b->set_layouts[cls] == VK_NULL_HANDLE)
        {
            if (!mel_gpu__class_layout_create(dev, cls, b->growable ? b->hw_max[cls] : b->caps[cls], &b->set_layouts[cls]))
                break;

            b->cur[cls] = mel_gpu__epoch_try(dev, cls, b->caps[cls], b->growable);
            if (b->cur[cls])
            {
                if (b->growable && b->caps[cls] < b->hw_max[cls])
                    b->srcs[cls] = mel_calloc(dev->alloc, sizeof(Mel_Gpu_Bindless_Slot_Src) * b->caps[cls]);
                break;
            }

            if (b->growable)
            {
                b->cur[cls] = mel_gpu__epoch_try(dev, cls, b->hw_max[cls], true);
                if (b->cur[cls])
                {
                    mel_log_info("gpu", "bindless: %s class pools are charged at layout size on this driver; allocated at the wall (%u descriptors) up front, class does not grow", mel_gpu__class_name[cls], b->hw_max[cls]);
                    b->caps[cls] = b->hw_max[cls];
                    break;
                }
                if (b->hw_max[cls] > b->caps[cls] * 2)
                {
                    vkDestroyDescriptorSetLayout(dev->vk, b->set_layouts[cls], NULL);
                    b->set_layouts[cls] = VK_NULL_HANDLE;
                    b->hw_max[cls] /= 2;
                    continue;
                }
            }
            break;
        }

        if (!b->cur[cls])
        {
            mel_log_error("gpu", "bindless: %s class could not allocate its descriptor set; heap disabled", mel_gpu__class_name[cls]);
            for (u32 k = 0; k <= cls; k++)
            {
                if (b->cur[k])
                    mel_gpu__epoch_destroy(dev, b->cur[k]);
                if (b->set_layouts[k])
                    vkDestroyDescriptorSetLayout(dev->vk, b->set_layouts[k], NULL);
                if (b->srcs[k])
                    mel_dealloc(dev->alloc, b->srcs[k]);
                if (k < cls)
                    mel_mutex_destroy(&b->locks[k]);
            }
            *b = (Mel_Gpu_Bindless){ 0 };
            return;
        }
        mel_mutex_init(&b->locks[cls], MEL_MUTEX_PLAIN);
    }

    b->enabled = true;

    dev->caps.memory.bindless.max_texture_view_slots = b->hw_max[MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE];
    dev->caps.memory.bindless.max_sampler_slots = b->hw_max[MEL_GPU_BINDLESS_CLASS_SAMPLER];
    dev->caps.memory.bindless.max_storage_buffer_slots = b->hw_max[MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER];
    dev->caps.memory.bindless.max_uniform_buffer_slots = b->hw_max[MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER];
    dev->caps.memory.bindless.max_storage_image_slots = b->hw_max[MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE];
    dev->caps.memory.bindless.seed_texture_view_slots = b->caps[MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE];
    dev->caps.memory.bindless.seed_sampler_slots = b->caps[MEL_GPU_BINDLESS_CLASS_SAMPLER];
    dev->caps.memory.bindless.seed_storage_buffer_slots = b->caps[MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER];
    dev->caps.memory.bindless.seed_uniform_buffer_slots = b->caps[MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER];
    dev->caps.memory.bindless.seed_storage_image_slots = b->caps[MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE];
    dev->caps.memory.bindless.growable = b->growable;

    mel_log_info("gpu", "bindless heap: per-class sets 0..%u, %s, seeds %u/%u/%u/%u/%u, walls %u/%u/%u/%u/%u",
                 (u32)MEL_GPU_BINDLESS_CLASS_COUNT - 1, b->growable ? "growable" : "fixed",
                 b->caps[0], b->caps[1], b->caps[2], b->caps[3], b->caps[4],
                 b->hw_max[0], b->hw_max[1], b->hw_max[2], b->hw_max[3], b->hw_max[4]);
}

void mel_gpu__bindless_shutdown(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled)
        return;
    for (u32 cls = 0; cls < MEL_GPU_BINDLESS_CLASS_COUNT; cls++)
    {
        if (b->cur[cls])
            mel_gpu__epoch_destroy(dev, b->cur[cls]);
        if (b->set_layouts[cls])
            vkDestroyDescriptorSetLayout(dev->vk, b->set_layouts[cls], NULL);
        if (b->srcs[cls])
            mel_dealloc(dev->alloc, b->srcs[cls]);
        mel_mutex_destroy(&b->locks[cls]);
    }
    b->enabled = false;
}

bool mel_gpu__bindless_slot_fits(Mel_Gpu_Device* dev, u32 binding_class, u32 slot)
{
    if (!dev->bindless.enabled)
        return true;
    return slot < mel_gpu__heap_cap_for_class(dev, binding_class);
}

static bool mel_gpu__bindless_grow(Mel_Gpu_Device* dev, u32 cls, u32 need_slot)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    u32               old_cap = b->caps[cls];
    u32               new_cap = mel_gpu__min_u32(mel_gpu__max_u32(old_cap * 2, need_slot + 1), b->hw_max[cls]);
    mel_assert(b->srcs[cls] && "bindless grow on a class without a re-publish side table");

    Mel_Gpu_Bindless_Epoch* fresh = mel_gpu__epoch_try(dev, cls, new_cap, false);
    if (!fresh)
        return false;

    u32                   live = 0;
    VkWriteDescriptorSet* writes = mel_alloc_array(dev->alloc, VkWriteDescriptorSet, old_cap);
    for (u32 slot = 0; slot < old_cap; slot++)
    {
        Mel_Gpu_Bindless_Slot_Src* src = &b->srcs[cls][slot];
        if (!src->live)
            continue;
        writes[live++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = fresh->set,
            .dstBinding = 0,
            .dstArrayElement = slot,
            .descriptorCount = 1,
            .descriptorType = mel_gpu__class_desc_type[cls],
            .pImageInfo = mel_gpu__class_is_image(cls) ? &src->image : NULL,
            .pBufferInfo = mel_gpu__class_is_image(cls) ? NULL : &src->buffer,
        };
    }
    if (live)
        vkUpdateDescriptorSets(dev->vk, live, writes, 0, NULL);
    mel_dealloc(dev->alloc, writes);

    Mel_Gpu_Bindless_Slot_Src* grown = mel_calloc(dev->alloc, sizeof(Mel_Gpu_Bindless_Slot_Src) * new_cap);
    for (u32 slot = 0; slot < old_cap; slot++)
        grown[slot] = b->srcs[cls][slot];
    mel_dealloc(dev->alloc, b->srcs[cls]);
    b->srcs[cls] = grown;

    Mel_Gpu_Bindless_Epoch* old = b->cur[cls];
    b->cur[cls] = fresh;
    b->caps[cls] = new_cap;

    old->superseded = true;
    if (old->refs == 0)
        mel_gpu__epoch_destroy(dev, old);

    mel_log_warn("gpu", "bindless: %s heap grew %u -> %u (%u live descriptors re-published)", mel_gpu__class_name[cls], old_cap, new_cap, live);
    return true;
}

static bool mel_gpu__bindless_write(Mel_Gpu_Device* dev, u32 cls, u32 slot, VkDescriptorImageInfo ii, VkDescriptorBufferInfo bi)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled)
        return false;

    mel_mutex_lock(&b->locks[cls]);
    if (slot >= b->caps[cls])
    {
        if (!b->growable || slot >= b->hw_max[cls] || !mel_gpu__bindless_grow(dev, cls, slot))
        {
            mel_mutex_unlock(&b->locks[cls]);
            mel_log_error("gpu", "bindless: %s slot %u exceeds the device wall %u (BindlessSlotExhausted); registration refused — migrate the class to an indirect/compacted heap", mel_gpu__class_name[cls], slot, b->hw_max[cls]);
            return false;
        }
    }

    VkWriteDescriptorSet w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = b->cur[cls]->set,
        .dstBinding = 0,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = mel_gpu__class_desc_type[cls],
        .pImageInfo = mel_gpu__class_is_image(cls) ? &ii : NULL,
        .pBufferInfo = mel_gpu__class_is_image(cls) ? NULL : &bi,
    };
    vkUpdateDescriptorSets(dev->vk, 1, &w, 0, NULL);
    if (b->srcs[cls])
        b->srcs[cls][slot] = (Mel_Gpu_Bindless_Slot_Src){ .image = ii, .buffer = bi, .live = true };
    mel_mutex_unlock(&b->locks[cls]);
    return true;
}

void mel_gpu__bindless_unregister(Mel_Gpu_Device* dev, u32 binding_class, u32 slot)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled || binding_class >= MEL_GPU_BINDLESS_CLASS_COUNT)
        return;
    mel_mutex_lock(&b->locks[binding_class]);
    if (b->srcs[binding_class] && slot < b->caps[binding_class])
        b->srcs[binding_class][slot].live = false;
    mel_mutex_unlock(&b->locks[binding_class]);
}

bool mel_gpu__bindless_register_sampled_image(Mel_Gpu_Device* dev, u32 slot, VkImageView view)
{
    return mel_gpu__bindless_write(dev, MEL_GPU_BINDLESS_CLASS_SAMPLED_IMAGE, slot, (VkDescriptorImageInfo){ .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, (VkDescriptorBufferInfo){ 0 });
}

bool mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 slot, VkImageView view)
{
    return mel_gpu__bindless_write(dev, MEL_GPU_BINDLESS_CLASS_STORAGE_IMAGE, slot, (VkDescriptorImageInfo){ .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL }, (VkDescriptorBufferInfo){ 0 });
}

bool mel_gpu__bindless_register_storage_buffer(Mel_Gpu_Device* dev, u32 slot, VkBuffer buf, VkDeviceSize range)
{
    return mel_gpu__bindless_write(dev, MEL_GPU_BINDLESS_CLASS_STORAGE_BUFFER, slot, (VkDescriptorImageInfo){ 0 }, (VkDescriptorBufferInfo){ .buffer = buf, .offset = 0, .range = range });
}

bool mel_gpu__bindless_register_uniform_buffer(Mel_Gpu_Device* dev, u32 slot, VkBuffer buf, VkDeviceSize range)
{
    return mel_gpu__bindless_write(dev, MEL_GPU_BINDLESS_CLASS_UNIFORM_BUFFER, slot, (VkDescriptorImageInfo){ 0 }, (VkDescriptorBufferInfo){ .buffer = buf, .offset = 0, .range = range });
}

bool mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 slot, VkSampler sampler)
{
    return mel_gpu__bindless_write(dev, MEL_GPU_BINDLESS_CLASS_SAMPLER, slot, (VkDescriptorImageInfo){ .sampler = sampler }, (VkDescriptorBufferInfo){ 0 });
}

void mel_gpu__bindless_epoch_release(Mel_Gpu_Device* dev, Mel_Gpu_Bindless_Epoch* epoch)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    mel_mutex_lock(&b->locks[epoch->cls]);
    mel_assert(epoch->refs > 0 && "bindless epoch over-released");
    epoch->refs--;
    bool gone = epoch->superseded && epoch->refs == 0;
    mel_mutex_unlock(&b->locks[epoch->cls]);
    if (gone)
        mel_gpu__epoch_destroy(dev, epoch);
}

void mel_gpu__bindless_cl_bind(Mel_Gpu_Command_List* cmd, VkPipelineBindPoint bind_point, VkPipelineLayout layout)
{
    Mel_Gpu_Device*   dev = cmd->dev;
    Mel_Gpu_Bindless* b = &dev->bindless;

    VkDescriptorSet sets[MEL_GPU_BINDLESS_CLASS_COUNT];
    for (u32 cls = 0; cls < MEL_GPU_BINDLESS_CLASS_COUNT; cls++)
    {
        mel_mutex_lock(&b->locks[cls]);
        Mel_Gpu_Bindless_Epoch* e = b->cur[cls];
        sets[cls] = e->set;
        bool held = false;
        for (u32 i = 0; i < cmd->held_count; i++)
            if (cmd->held_epochs[i] == e)
            {
                held = true;
                break;
            }
        if (!held)
        {
            e->refs++;
            if (cmd->held_count == cmd->held_cap)
            {
                u32 cap = cmd->held_cap ? cmd->held_cap * 2 : MEL_GPU_BINDLESS_CLASS_COUNT;
                cmd->held_epochs = cmd->held_epochs ? mel_realloc(dev->alloc, cmd->held_epochs, sizeof(*cmd->held_epochs) * cap) : mel_alloc(dev->alloc, sizeof(*cmd->held_epochs) * cap);
                cmd->held_cap = cap;
            }
            cmd->held_epochs[cmd->held_count++] = e;
        }
        mel_mutex_unlock(&b->locks[cls]);
    }
    vkCmdBindDescriptorSets(cmd->cb, bind_point, layout, 0, MEL_GPU_BINDLESS_CLASS_COUNT, sets, 0, NULL);
}

void mel_gpu__bindless_cl_release(Mel_Gpu_Command_List* cmd)
{
    for (u32 i = 0; i < cmd->held_count; i++)
        mel_gpu__bindless_epoch_release(cmd->dev, cmd->held_epochs[i]);
    cmd->held_count = 0;
}

void mel_gpu__bindless_cl_transfer(Mel_Gpu_Command_List* cmd, u64 serial)
{
    for (u32 i = 0; i < cmd->held_count; i++)
        mel_gpu__defer_free_marked(cmd->dev, (Mel_Gpu_Deferred_Free){ .bindless_epoch = cmd->held_epochs[i] }, serial);
    cmd->held_count = 0;
}

bool mel_gpu_bindless_available(Mel_Gpu_Device* dev) { return dev && dev->bindless.enabled; }

u32 mel_gpu_texture_view_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view)
{
    Mel_Gpu_Texture_View_Obj o;
    if (!dev || !mel_gpu__texture_view_get(dev, view, &o))
    {
        mel_assert(!"texture_view_bindless_slot: invalid view handle");
        return 0;
    }
    mel_assert(dev->bindless.enabled && "bindless heap not enabled on this device");
    return view.slot.index;
}

u32 mel_gpu_buffer_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    mel_assert(dev && mel_gpu_buffer_alive(dev, buf) && "buffer_bindless_slot: invalid buffer handle");
    mel_assert(dev->bindless.enabled && "bindless heap not enabled on this device");
    return buf.slot.index;
}

void mel_gpu_cmd_bind_bindless(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd);
    Mel_Gpu_Device* dev = cmd->dev;
    if (!dev->bindless.enabled)
    {
        mel_assert(!"cmd_bind_bindless: bindless heap not enabled");
        return;
    }
    mel_assert(cmd->cur_layout != VK_NULL_HANDLE && "cmd_bind_bindless: bind a pipeline first");
    mel_gpu__bindless_cl_bind(cmd, cmd->cur_bind_point, cmd->cur_layout);
}
