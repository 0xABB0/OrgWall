#include "vk_backend.h"

#include <gpu/texture.h>
#include <log/log.h>

#include <string.h>

static VkImageUsageFlags mel_gpu__texture_usage(Mel_Gpu_Texture_Usage u, bool is_depth)
{
    VkImageUsageFlags f = 0;
    if (u & MEL_GPU_TEXTURE_SAMPLED)
        f |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (u & MEL_GPU_TEXTURE_STORAGE)
        f |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (u & MEL_GPU_TEXTURE_ATTACHMENT)
        f |= is_depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (u & MEL_GPU_TEXTURE_COPY_SRC)
        f |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (u & MEL_GPU_TEXTURE_COPY_DST)
        f |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (u & MEL_GPU_TEXTURE_TRANSIENT)
        f |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    return f;
}

static VkImageType mel_gpu__image_type(Mel_Gpu_Texture_Kind kind)
{
    switch (kind)
    {
    case MEL_GPU_TEXTURE_1D:
        return VK_IMAGE_TYPE_1D;
    case MEL_GPU_TEXTURE_3D:
        return VK_IMAGE_TYPE_3D;
    case MEL_GPU_TEXTURE_2D:
    default:
        return VK_IMAGE_TYPE_2D;
    }
}

static VkImageViewType mel_gpu__view_type(Mel_Gpu_View_Dimension dim)
{
    switch (dim)
    {
    case MEL_GPU_VIEW_1D:
        return VK_IMAGE_VIEW_TYPE_1D;
    case MEL_GPU_VIEW_1D_ARRAY:
        return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case MEL_GPU_VIEW_2D_ARRAY:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case MEL_GPU_VIEW_3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    case MEL_GPU_VIEW_CUBE:
        return VK_IMAGE_VIEW_TYPE_CUBE;
    case MEL_GPU_VIEW_CUBE_ARRAY:
        return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case MEL_GPU_VIEW_2D:
    default:
        return VK_IMAGE_VIEW_TYPE_2D;
    }
}

VkImageAspectFlags mel_gpu__aspect_flags(Mel_Gpu_Texture_Aspect aspect, VkFormat fmt)
{
    bool has_stencil = fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT || fmt == VK_FORMAT_S8_UINT;
    bool has_depth = fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT || fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT;

    switch (aspect)
    {
    case MEL_GPU_ASPECT_COLOR:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    case MEL_GPU_ASPECT_DEPTH:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case MEL_GPU_ASPECT_STENCIL:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    case MEL_GPU_ASPECT_DEPTH_AND_STENCIL:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case MEL_GPU_ASPECT_PLANE0:
        return VK_IMAGE_ASPECT_PLANE_0_BIT;
    case MEL_GPU_ASPECT_PLANE1:
        return VK_IMAGE_ASPECT_PLANE_1_BIT;
    case MEL_GPU_ASPECT_PLANE2:
        return VK_IMAGE_ASPECT_PLANE_2_BIT;
    case MEL_GPU_ASPECT_DEFAULT:
    default:
        break;
    }
    // Primary aspect of the format.
    if (has_depth && has_stencil)
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (has_depth)
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    if (has_stencil)
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

Mel_Gpu_Texture_Create_Result mel_gpu_texture_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Texture_Opt opt)
{
    Mel_Gpu_Texture_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_TEXTURE_CREATE_OK };

    if (!dev || opt.extent.width == 0 || opt.format == MEL_GPU_FORMAT_UNDEFINED)
    {
        res.status = MEL_GPU_TEXTURE_CREATE_BAD_PARAMS;
        mel_log_error("gpu", "texture_create: bad params (extent/format)");
        return res;
    }

    u32      mips = opt.mip_levels ? opt.mip_levels : 1;
    u32      layers = opt.array_layers ? opt.array_layers : 1;
    u32      samples = opt.sample_count ? opt.sample_count : 1;
    VkFormat vkfmt = mel_gpu__vk_format(opt.format);
    bool     is_depth = mel_gpu_format_is_depth(opt.format);

    if (opt.memory != MEL_GPU_MEMORY_DEVICE)
        mel_log_warn("gpu", "texture_create '%s': memory role %d ignored — textures are device-local optimal in this slice", opt.name ? opt.name : "(unnamed)", (int)opt.memory);

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = mel_gpu__image_type(opt.kind),
        .format = vkfmt,
        .extent = { opt.extent.width, opt.extent.height ? opt.extent.height : 1, opt.extent.depth ? opt.extent.depth : 1 },
        .mipLevels = mips,
        .arrayLayers = layers,
        .samples = (VkSampleCountFlagBits)samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = mel_gpu__texture_usage(opt.usage, is_depth),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .flags = opt.cube_compatible ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
    };

    VkImage  image = VK_NULL_HANDLE;
    VkResult r = vkCreateImage(dev->vk, &ici, NULL, &image);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkCreateImage failed: %s", mel_gpu__vk_result_str(r));
        res.status = MEL_GPU_TEXTURE_CREATE_VK_FAILED;
        return res;
    }

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev->vk, image, &req);

    Mel_Gpu_Texture_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.capture_replay = opt.capture_replay;
    obj.header.name = opt.name;
    obj.image = image;
    obj.format = vkfmt;
    obj.aspect = mel_gpu__aspect_flags(MEL_GPU_ASPECT_DEFAULT, vkfmt);
    obj.image_type = ici.imageType;
    obj.width = ici.extent.width;
    obj.height = ici.extent.height;
    obj.depth = ici.extent.depth;
    obj.mip_levels = mips;
    obj.array_layers = layers;
    obj.sample_count = samples;
    obj.usage = ici.usage;

    if (!mel_gpu__mem_alloc(dev, req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &obj.alloc))
    {
        vkDestroyImage(dev->vk, image, NULL);
        res.status = MEL_GPU_TEXTURE_CREATE_OOM;
        mel_log_error("gpu", "texture_create '%s': out of device memory", opt.name ? opt.name : "(unnamed)");
        return res;
    }
    vkBindImageMemory(dev->vk, image, obj.alloc.mem, obj.alloc.offset);

    res.value.slot = mel_gpu__table_insert(dev, &dev->textures, &obj);
    return res;
}

void mel_gpu_texture_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex)
{
    // §3.7: texture_destroy is SerializedPerObject on the destroyed handle.
    const void* trk = mel_gpu__track_key(&dev->textures, tex.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    Mel_Gpu_Texture_Obj o; // BUG-1: copy the record out under obj_lock; never deref the packed slot post-unlock
    if (!mel_gpu__table_get_copy(dev, &dev->textures, tex.slot, &o))
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    Mel_Gpu_Allocation alloc = o.alloc;
    VkImage            image = o.image;
    bool               borrowed = o.header.ownership == MEL_GPU_OWNERSHIP_BORROWED;
    mel_gpu__table_remove(dev, &dev->textures, tex.slot);
    if (!borrowed)
        mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .image = image, .alloc = alloc, .has_alloc = true });
    mel_gpu__track_exit(dev, trk);
}

bool mel_gpu_texture_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex) { return mel_gpu__table_alive(dev, &dev->textures, tex.slot); }

// BUG-1: fill the caller's *out record by value under obj_lock. Texture/view records are immutable post-insert,
// so the local copy is a faithful, race-free snapshot; the caller keeps a pointer to its own stack storage.
bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj* out)
{
    return mel_gpu__table_get_copy(dev, &dev->textures, tex.slot, out);
}

bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj* out)
{
    return mel_gpu__table_get_copy(dev, &dev->texture_views, view.slot, out);
}

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_view_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View_Opt opt)
{
    Mel_Gpu_Texture_View_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_TEXTURE_VIEW_CREATE_OK };

    Mel_Gpu_Texture_Obj  tex_obj;
    Mel_Gpu_Texture_Obj* tex = &tex_obj;
    if (!dev || !mel_gpu__texture_get(dev, opt.texture, tex))
    {
        res.status = MEL_GPU_TEXTURE_VIEW_CREATE_BAD_TEXTURE;
        mel_log_error("gpu", "texture_view_create: invalid texture handle");
        return res;
    }

    VkFormat           vkfmt = opt.format == MEL_GPU_FORMAT_UNDEFINED ? tex->format : mel_gpu__vk_format(opt.format);
    VkImageAspectFlags aspect = mel_gpu__aspect_flags(opt.range.aspect, vkfmt);
    u32                mip_count = opt.range.mip_count ? opt.range.mip_count : (tex->mip_levels - opt.range.base_mip);
    u32                layer_count = opt.range.layer_count ? opt.range.layer_count : (tex->array_layers - opt.range.base_layer);
    VkImage            image = tex->image;

    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = mel_gpu__view_type(opt.dimension),
        .format = vkfmt,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = opt.range.base_mip,
            .levelCount = mip_count,
            .baseArrayLayer = opt.range.base_layer,
            .layerCount = layer_count,
        },
    };

    VkImageView view = VK_NULL_HANDLE;
    VkResult    r = vkCreateImageView(dev->vk, &vci, NULL, &view);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "vkCreateImageView failed: %s", mel_gpu__vk_result_str(r));
        res.status = MEL_GPU_TEXTURE_VIEW_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Texture_View_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.view = view;
    obj.texture = opt.texture.slot;
    obj.format = vkfmt;
    obj.aspect = aspect;
    obj.base_mip = opt.range.base_mip;
    obj.mip_count = mip_count;
    obj.base_layer = opt.range.base_layer;
    obj.layer_count = layer_count;

    res.value.slot = mel_gpu__table_insert(dev, &dev->texture_views, &obj);

    // U14: the view owns the bindless slot (gpu-rhi.md §6.2). Auto-register it into the device heap at the
    // handle index for every shader-readable class the parent texture's usage admits (direct contract §3.1).
    // CRITICAL-1: pre-flight EVERY class the view registers into before writing any descriptor, so an over-cap
    // slot fails the create loudly (BindlessSlotExhausted) and rolls back — no partial heap write, no unbound
    // slot reported as OK (MEL-ENGINE-VIII).
    if (dev->bindless.enabled)
    {
        bool sampled = (tex->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
        bool storage = (tex->usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
        bool fits = true;
        if (sampled)
            fits = mel_gpu__bindless_slot_fits(dev, MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, res.value.slot.index) && fits;
        if (storage)
            fits = mel_gpu__bindless_slot_fits(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, res.value.slot.index) && fits;
        if (!fits)
        {
            mel_log_error("gpu", "texture_view_create '%s': bindless slot %u exceeds a heap class cap (BindlessSlotExhausted)", opt.name ? opt.name : "(unnamed)", res.value.slot.index);
            mel_gpu__table_remove(dev, &dev->texture_views, res.value.slot);
            vkDestroyImageView(dev->vk, view, NULL);
            res.value = (Mel_Gpu_Texture_View){ mel_gpu_handle_null() };
            res.status = MEL_GPU_TEXTURE_VIEW_CREATE_BINDLESS_SLOT_EXHAUSTED;
            return res;
        }
        if (sampled)
            mel_gpu__bindless_register_sampled_image(dev, res.value.slot.index, view);
        if (storage)
            mel_gpu__bindless_register_storage_image(dev, res.value.slot.index, view);
    }
    return res;
}

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_default_view(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex)
{
    Mel_Gpu_Texture_Obj    o;
    Mel_Gpu_View_Dimension dim = MEL_GPU_VIEW_2D;
    if (dev && mel_gpu__texture_get(dev, tex, &o))
    {
        if (o.image_type == VK_IMAGE_TYPE_1D)
            dim = o.array_layers > 1 ? MEL_GPU_VIEW_1D_ARRAY : MEL_GPU_VIEW_1D;
        else if (o.image_type == VK_IMAGE_TYPE_3D)
            dim = MEL_GPU_VIEW_3D;
        else
            dim = o.array_layers > 1 ? MEL_GPU_VIEW_2D_ARRAY : MEL_GPU_VIEW_2D;
    }
    return mel_gpu_texture_view_create_opt(dev, (Mel_Gpu_Texture_View_Opt){ .texture = tex, .dimension = dim });
}

void mel_gpu_texture_view_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view)
{
    // §3.7: texture_view_destroy is SerializedPerObject on the destroyed handle.
    const void* trk = mel_gpu__track_key(&dev->texture_views, view.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    Mel_Gpu_Texture_View_Obj o; // BUG-1: snapshot under obj_lock before any deref
    if (!mel_gpu__table_get_copy(dev, &dev->texture_views, view.slot, &o))
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    VkImageView vk = o.view;
    bool        borrowed = o.header.ownership == MEL_GPU_OWNERSHIP_BORROWED;
    if (borrowed)
    {
        mel_gpu__table_remove(dev, &dev->texture_views, view.slot);
    }
    else
    {
        // U14: the view owns its bindless slot, so the slot index is reclaimed on the same retirement edge as
        // the VkImageView — never reused while an in-flight draw still samples it (gpu-rhi.md §3.3 / §6.7).
        mel_gpu__table_remove_deferred(dev, &dev->texture_views, view.slot);
        mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .view = vk, .reclaim_table = &dev->texture_views, .reclaim_index = view.slot.index, .has_reclaim = true });
    }
    mel_gpu__track_exit(dev, trk);
}

bool mel_gpu_texture_view_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view) { return mel_gpu__table_alive(dev, &dev->texture_views, view.slot); }

void mel_gpu_texture_write(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Region region, const void* data, usize bytes)
{
    Mel_Gpu_Texture_Obj  o_obj; // BUG-1: snapshot under obj_lock; the immutable texture record is read by value
    Mel_Gpu_Texture_Obj* o = &o_obj;
    if (!dev || !mel_gpu__texture_get(dev, tex, o))
    {
        mel_assert(!"texture_write: invalid texture handle");
        return;
    }

    // §3.7: texture_write is SerializedPerObject on this resource (concurrent on distinct resources).
    const void* trk = mel_gpu__track_key(&dev->textures, tex.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);

    // Staging buffer (host-image-copy fast path is cap-gated and not in this slice — gpu-rhi.md §6.2).
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(dev->vk, &bci, NULL, &staging) != VK_SUCCESS)
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev->vk, staging, &req);
    Mel_Gpu_Allocation sa;
    if (!mel_gpu__mem_alloc(dev, req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &sa))
    {
        vkDestroyBuffer(dev->vk, staging, NULL);
        mel_gpu__track_exit(dev, trk);
        return;
    }
    vkBindBufferMemory(dev->vk, staging, sa.mem, sa.offset);
    memcpy(sa.mapped, data, bytes);

    VkImageAspectFlags aspect = mel_gpu__aspect_flags(region.subresource.aspect, o->format);
    VkExtent3D         extent = { region.extent.width ? region.extent.width : o->width, region.extent.height ? region.extent.height : o->height, region.extent.depth ? region.extent.depth : 1 };

    VkCommandPool               pool = mel_gpu__thread_pool(dev, dev->graphics_family);
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer             cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev->vk, &cai, &cb);

    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cb, &begin);

    VkImageMemoryBarrier to_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = o->image,
        .subresourceRange = { aspect, region.subresource.base_mip, 1, region.subresource.base_layer, region.subresource.layer_count ? region.subresource.layer_count : 1 },
    };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_dst);

    VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { aspect, region.subresource.base_mip, region.subresource.base_layer, region.subresource.layer_count ? region.subresource.layer_count : 1 },
        .imageOffset = { (i32)region.offset.width, (i32)region.offset.height, (i32)region.offset.depth },
        .imageExtent = extent,
    };
    vkCmdCopyBufferToImage(cb, staging, o->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier to_read = to_dst;
    to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &to_read);

    vkEndCommandBuffer(cb);

    // MAJOR-4: reserve a serial so the upload participates in the retirement watermark (§3.3) instead of being
    // invisible to the engine's retirement clock (a latent UAF once uploads go async); the staging buffer + its
    // memory are routed through the deferred-free queue rather than freed raw. The synchronous contract is met
    // by waiting on this upload's OWN fence, not a full-queue vkQueueWaitIdle — a queue-wide idle serializes
    // every unrelated submission on the single graphics queue (MEL-ENGINE-III/VI). The fence signal is the exact
    // drain edge, so advancing the watermark to this serial retires the staging free immediately and exactly.
    u64               serial = mel_gpu__submit_serial_next(dev);
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence           fence = VK_NULL_HANDLE;
    vkCreateFence(dev->vk, &fci, NULL, &fence);
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    mel_mutex_lock(&dev->submit_lock);
    vkQueueSubmit(dev->graphics_queue, 1, &si, fence);
    mel_mutex_unlock(&dev->submit_lock);
    vkWaitForFences(dev->vk, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(dev->vk, fence, NULL);
    vkFreeCommandBuffers(dev->vk, pool, 1, &cb);
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .buffer = staging, .alloc = sa, .has_alloc = true });
    mel_gpu__submit_complete(dev, serial);
    mel_gpu__track_exit(dev, trk);
}
