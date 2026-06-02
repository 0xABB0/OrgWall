#include "vk_backend.h"

#include <log/log.h>

// U14 device-global bindless heap (gpu-rhi.md §6.7). One descriptor set; one large partially-bound
// update-after-bind array per resource class. This is the descriptor-indexing floor — the earlier Vulkan
// ceiling — reported as caps.memory.bindless.tier = full. The descriptor_buffer / descriptor_heap path is
// a later additive lowering of the same public surface.

static u32 mel_gpu__min_u32(u32 a, u32 b) { return a < b ? a : b; }

void mel_gpu__bindless_init(Mel_Gpu_Device* dev, bool want)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    *b = (Mel_Gpu_Bindless){ 0 };

    if (!want || dev->caps.memory.bindless.tier == MEL_GPU_TIER_NONE)
        return;

    // Heap caps: a sane default clamped to the device's per-stage update-after-bind limits. The slot count
    // bounds the addressable handle index; the engine never silently compacts past it (gpu-rhi.md §6.2).
    const u32 DEFAULT_IMAGE = 16384, DEFAULT_SAMPLER = 2048, DEFAULT_BUFFER = 16384;
    b->cap_sampled_image = mel_gpu__min_u32(DEFAULT_IMAGE, dev->caps.memory.bindless.max_texture_view_slots);
    b->cap_sampler = mel_gpu__min_u32(DEFAULT_SAMPLER, dev->caps.memory.bindless.max_sampler_slots);
    b->cap_storage_buffer = mel_gpu__min_u32(DEFAULT_BUFFER, dev->caps.memory.bindless.max_storage_buffer_slots);
    b->cap_uniform_buffer = mel_gpu__min_u32(DEFAULT_BUFFER, dev->caps.memory.bindless.max_uniform_buffer_slots);
    b->cap_storage_image = mel_gpu__min_u32(DEFAULT_IMAGE, dev->caps.memory.bindless.max_storage_image_slots);

    VkDescriptorSetLayoutBinding bindings[MEL_GPU_BINDLESS_BINDING_COUNT] = {
        { MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, b->cap_sampled_image, VK_SHADER_STAGE_ALL, NULL },
        { MEL_GPU_BINDLESS_BINDING_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLER, b->cap_sampler, VK_SHADER_STAGE_ALL, NULL },
        { MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, b->cap_storage_buffer, VK_SHADER_STAGE_ALL, NULL },
        { MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, b->cap_uniform_buffer, VK_SHADER_STAGE_ALL, NULL },
        { MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, b->cap_storage_image, VK_SHADER_STAGE_ALL, NULL },
    };
    VkDescriptorBindingFlags flag = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    VkDescriptorBindingFlags flags[MEL_GPU_BINDLESS_BINDING_COUNT] = { flag, flag, flag, flag, flag };
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = MEL_GPU_BINDLESS_BINDING_COUNT,
        .pBindingFlags = flags,
    };
    VkDescriptorSetLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flags_ci,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = MEL_GPU_BINDLESS_BINDING_COUNT,
        .pBindings = bindings,
    };
    if (vkCreateDescriptorSetLayout(dev->vk, &lci, NULL, &b->set_layout) != VK_SUCCESS)
    {
        mel_log_error("gpu", "bindless: vkCreateDescriptorSetLayout failed; heap disabled");
        return;
    }

    VkDescriptorPoolSize sizes[MEL_GPU_BINDLESS_BINDING_COUNT] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, b->cap_sampled_image },
        { VK_DESCRIPTOR_TYPE_SAMPLER, b->cap_sampler },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, b->cap_storage_buffer },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, b->cap_uniform_buffer },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, b->cap_storage_image },
    };
    VkDescriptorPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = MEL_GPU_BINDLESS_BINDING_COUNT,
        .pPoolSizes = sizes,
    };
    if (vkCreateDescriptorPool(dev->vk, &pci, NULL, &b->pool) != VK_SUCCESS)
    {
        mel_log_error("gpu", "bindless: vkCreateDescriptorPool failed; heap disabled");
        vkDestroyDescriptorSetLayout(dev->vk, b->set_layout, NULL);
        b->set_layout = VK_NULL_HANDLE;
        return;
    }

    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = b->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &b->set_layout,
    };
    if (vkAllocateDescriptorSets(dev->vk, &ai, &b->set) != VK_SUCCESS)
    {
        mel_log_error("gpu", "bindless: vkAllocateDescriptorSets failed; heap disabled");
        vkDestroyDescriptorPool(dev->vk, b->pool, NULL);
        vkDestroyDescriptorSetLayout(dev->vk, b->set_layout, NULL);
        b->pool = VK_NULL_HANDLE;
        b->set_layout = VK_NULL_HANDLE;
        return;
    }

    mel_mutex_init(&b->lock, MEL_MUTEX_PLAIN);
    b->enabled = true;

    // caps now report the realized heap capacity, not the device's theoretical maximum.
    dev->caps.memory.bindless.max_texture_view_slots = b->cap_sampled_image;
    dev->caps.memory.bindless.max_sampler_slots = b->cap_sampler;
    dev->caps.memory.bindless.max_storage_buffer_slots = b->cap_storage_buffer;
    dev->caps.memory.bindless.max_uniform_buffer_slots = b->cap_uniform_buffer;
    dev->caps.memory.bindless.max_storage_image_slots = b->cap_storage_image;

    mel_log_info("gpu", "bindless heap: %u sampled images, %u samplers, %u storage buffers (descriptor-indexing floor)", b->cap_sampled_image, b->cap_sampler, b->cap_storage_buffer);
}

void mel_gpu__bindless_shutdown(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled)
        return;
    // The pool frees the set; the layout is destroyed explicitly. Both are device-lifetime, no deferral.
    if (b->pool)
        vkDestroyDescriptorPool(dev->vk, b->pool, NULL);
    if (b->set_layout)
        vkDestroyDescriptorSetLayout(dev->vk, b->set_layout, NULL);
    mel_mutex_destroy(&b->lock);
    b->enabled = false;
}

// Heap writes follow update-after-bind semantics. Writing a slot an in-flight submission samples is
// undefined (gpu-rhi.md §6.7); the engine never writes a live slot — register happens at create, the slot
// is reused only after the destroy's completion future resolves (§3.3). Past the cap the slot is dropped
// loudly rather than silently aliasing another resource (MEL-ENGINE-VIII).
static bool mel_gpu__bindless_check(Mel_Gpu_Device* dev, u32 slot, u32 cap, const char* klass)
{
    if (!dev->bindless.enabled)
        return false;
    if (slot >= cap)
    {
        mel_log_error("gpu", "bindless: %s slot %u exceeds heap capacity %u (BindlessSlotExhausted)", klass, slot, cap);
        mel_assert(!"bindless slot exceeds heap capacity");
        return false;
    }
    return true;
}

void mel_gpu__bindless_register_sampled_image(Mel_Gpu_Device* dev, u32 slot, VkImageView view)
{
    if (!mel_gpu__bindless_check(dev, slot, dev->bindless.cap_sampled_image, "sampled-image"))
        return;
    VkDescriptorImageInfo ii = { .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dev->bindless.set,
        .dstBinding = MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &ii,
    };
    mel_mutex_lock(&dev->bindless.lock);
    vkUpdateDescriptorSets(dev->vk, 1, &w, 0, NULL);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 slot, VkImageView view)
{
    if (!mel_gpu__bindless_check(dev, slot, dev->bindless.cap_storage_image, "storage-image"))
        return;
    VkDescriptorImageInfo ii = { .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dev->bindless.set,
        .dstBinding = MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &ii,
    };
    mel_mutex_lock(&dev->bindless.lock);
    vkUpdateDescriptorSets(dev->vk, 1, &w, 0, NULL);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_storage_buffer(Mel_Gpu_Device* dev, u32 slot, VkBuffer buf, VkDeviceSize range)
{
    if (!mel_gpu__bindless_check(dev, slot, dev->bindless.cap_storage_buffer, "storage-buffer"))
        return;
    VkDescriptorBufferInfo bi = { .buffer = buf, .offset = 0, .range = range };
    VkWriteDescriptorSet w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dev->bindless.set,
        .dstBinding = MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &bi,
    };
    mel_mutex_lock(&dev->bindless.lock);
    vkUpdateDescriptorSets(dev->vk, 1, &w, 0, NULL);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_uniform_buffer(Mel_Gpu_Device* dev, u32 slot, VkBuffer buf, VkDeviceSize range)
{
    if (!mel_gpu__bindless_check(dev, slot, dev->bindless.cap_uniform_buffer, "uniform-buffer"))
        return;
    VkDescriptorBufferInfo bi = { .buffer = buf, .offset = 0, .range = range };
    VkWriteDescriptorSet w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dev->bindless.set,
        .dstBinding = MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &bi,
    };
    mel_mutex_lock(&dev->bindless.lock);
    vkUpdateDescriptorSets(dev->vk, 1, &w, 0, NULL);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 slot, VkSampler sampler)
{
    if (!mel_gpu__bindless_check(dev, slot, dev->bindless.cap_sampler, "sampler"))
        return;
    VkDescriptorImageInfo ii = { .sampler = sampler };
    VkWriteDescriptorSet w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dev->bindless.set,
        .dstBinding = MEL_GPU_BINDLESS_BINDING_SAMPLER,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &ii,
    };
    mel_mutex_lock(&dev->bindless.lock);
    vkUpdateDescriptorSets(dev->vk, 1, &w, 0, NULL);
    mel_mutex_unlock(&dev->bindless.lock);
}

// ---- public surface ----

bool mel_gpu_bindless_available(Mel_Gpu_Device* dev) { return dev && dev->bindless.enabled; }

u32 mel_gpu_texture_view_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view)
{
    Mel_Gpu_Texture_View_Obj* o = NULL;
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
    vkCmdBindDescriptorSets(cmd->cb, cmd->cur_bind_point, cmd->cur_layout, 0, 1, &dev->bindless.set, 0, NULL);
}
