#include "vk_backend.h"

#include <gpu/sampler.h>
#include <log/log.h>

#include <string.h>

// U11 samplers (gpu-rhi.md §6.3). Auto-deduplicating: one VkSampler per unique descriptor across the
// device, reference-counted. The descriptor is a closed protocol mapping onto the backend sampler object.

static VkFilter mel_gpu__filter(Mel_Gpu_Filter f) { return f == MEL_GPU_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; }
static VkSamplerMipmapMode mel_gpu__mip(Mel_Gpu_Mipmap_Mode m) { return m == MEL_GPU_MIPMAP_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST; }

static VkSamplerAddressMode mel_gpu__wrap(Mel_Gpu_Wrap w)
{
    switch (w)
    {
    case MEL_GPU_WRAP_MIRROR_REPEAT:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case MEL_GPU_WRAP_CLAMP_EDGE:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case MEL_GPU_WRAP_CLAMP_BORDER:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case MEL_GPU_WRAP_REPEAT:
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkBorderColor mel_gpu__border(Mel_Gpu_Border_Color b)
{
    switch (b)
    {
    case MEL_GPU_BORDER_OPAQUE_BLACK:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case MEL_GPU_BORDER_OPAQUE_WHITE:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    case MEL_GPU_BORDER_TRANSPARENT_BLACK:
    default:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}

// Resolve the request into the canonical key (clamps anisotropy to the device limit), then hash the key.
static Mel_Gpu_Sampler_Key mel_gpu__sampler_key(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt)
{
    f32 aniso = opt.max_anisotropy;
    if (aniso < 1.0f)
        aniso = 1.0f;
    if (aniso > dev->max_sampler_anisotropy)
        aniso = dev->max_sampler_anisotropy;
    return (Mel_Gpu_Sampler_Key){
        .min_filter = (u8)opt.min_filter,
        .mag_filter = (u8)opt.mag_filter,
        .mip_filter = (u8)opt.mip_filter,
        .wrap_u = (u8)opt.wrap_u,
        .wrap_v = (u8)opt.wrap_v,
        .wrap_w = (u8)opt.wrap_w,
        .compare = (u8)opt.compare,
        .border = (u8)opt.border,
        .max_anisotropy = aniso,
        .lod_min = opt.lod_min,
        .lod_max = opt.lod_max,
    };
}

static u64 mel_gpu__hash_key(const Mel_Gpu_Sampler_Key* k)
{
    const u8* p = (const u8*)k;
    u64       h = 1469598103934665603ull; // FNV-1a 64
    for (usize i = 0; i < sizeof *k; i++)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

Mel_Gpu_Sampler_Create_Result mel_gpu_sampler_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt)
{
    Mel_Gpu_Sampler_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SAMPLER_CREATE_OK };
    if (!dev)
    {
        res.status = MEL_GPU_SAMPLER_CREATE_BAD_PARAMS;
        return res;
    }

    Mel_Gpu_Sampler_Key key = mel_gpu__sampler_key(dev, opt);
    u64                 hash = mel_gpu__hash_key(&key);

    mel_mutex_lock(&dev->sampler_lock);

    // Dedup: an identical descriptor returns the shared handle and takes one more logical claim.
    for (u32 i = 0; i < dev->sampler_intern_count; i++)
    {
        if (dev->sampler_interns[i].hash != hash)
            continue;
        Mel_Gpu_Sampler_Obj* o = mel_gpu__table_get(dev, &dev->samplers, dev->sampler_interns[i].handle);
        if (o && memcmp(&o->key, &key, sizeof key) == 0)
        {
            o->refcount++;
            res.value.slot = dev->sampler_interns[i].handle;
            mel_mutex_unlock(&dev->sampler_lock);
            return res;
        }
    }

    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mel_gpu__filter(opt.mag_filter),
        .minFilter = mel_gpu__filter(opt.min_filter),
        .mipmapMode = mel_gpu__mip(opt.mip_filter),
        .addressModeU = mel_gpu__wrap(opt.wrap_u),
        .addressModeV = mel_gpu__wrap(opt.wrap_v),
        .addressModeW = mel_gpu__wrap(opt.wrap_w),
        .anisotropyEnable = key.max_anisotropy > 1.0f ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = key.max_anisotropy,
        .compareEnable = opt.compare != MEL_GPU_COMPARE_NONE ? VK_TRUE : VK_FALSE,
        .compareOp = mel_gpu__vk_compare_op(opt.compare),
        .borderColor = mel_gpu__border(opt.border),
        .minLod = opt.lod_min,
        .maxLod = opt.lod_max > 0.0f ? opt.lod_max : VK_LOD_CLAMP_NONE,
    };

    VkSampler vk = VK_NULL_HANDLE;
    VkResult  r = vkCreateSampler(dev->vk, &sci, NULL, &vk);
    if (r != VK_SUCCESS)
    {
        mel_mutex_unlock(&dev->sampler_lock);
        mel_log_error("gpu", "vkCreateSampler failed: %s", mel_gpu__vk_result_str(r));
        res.status = MEL_GPU_SAMPLER_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Sampler_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.sampler = vk;
    obj.key = key;
    obj.hash = hash;
    obj.refcount = 1;
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->samplers, &obj);

    if (dev->sampler_intern_count == dev->sampler_intern_cap)
    {
        u32 cap = dev->sampler_intern_cap ? dev->sampler_intern_cap * 2 : 16;
        dev->sampler_interns = dev->sampler_interns ? mel_realloc(dev->alloc, dev->sampler_interns, sizeof(Mel_Gpu_Sampler_Intern) * cap) : mel_alloc(dev->alloc, sizeof(Mel_Gpu_Sampler_Intern) * cap);
        dev->sampler_intern_cap = cap;
    }
    dev->sampler_interns[dev->sampler_intern_count++] = (Mel_Gpu_Sampler_Intern){ .hash = hash, .handle = h };

    mel_mutex_unlock(&dev->sampler_lock);

    // U14: the sampler's persistent heap slot is its handle index (direct contract, §3.1).
    mel_gpu__bindless_register_sampler(dev, h.index, vk);

    res.value.slot = h;
    return res;
}

void mel_gpu_sampler_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    mel_mutex_lock(&dev->sampler_lock);
    Mel_Gpu_Sampler_Obj* o = mel_gpu__table_get(dev, &dev->samplers, sampler.slot);
    if (!o)
    {
        mel_mutex_unlock(&dev->sampler_lock);
        return;
    }
    if (--o->refcount > 0)
    {
        // Other logical claims remain; the slot and descriptor stay live.
        mel_mutex_unlock(&dev->sampler_lock);
        return;
    }

    VkSampler vk = o->sampler;
    u64       hash = o->hash;
    for (u32 i = 0; i < dev->sampler_intern_count; i++)
        if (dev->sampler_interns[i].hash == hash && mel_gpu_handle_eq(dev->sampler_interns[i].handle, sampler.slot))
        {
            dev->sampler_interns[i] = dev->sampler_interns[--dev->sampler_intern_count];
            break;
        }
    mel_gpu__table_remove_deferred(dev, &dev->samplers, sampler.slot);
    mel_mutex_unlock(&dev->sampler_lock);

    // U3/U14: the descriptor may still be sampled by an in-flight submission — free the VkSampler and
    // reclaim the heap slot index only after it retires (gpu-rhi.md §3.3).
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .sampler = vk, .reclaim_table = &dev->samplers, .reclaim_index = sampler.slot.index, .has_reclaim = true });
}

bool mel_gpu_sampler_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    mel_mutex_lock(&dev->sampler_lock);
    bool alive = mel_gpu__table_get(dev, &dev->samplers, sampler.slot) != NULL;
    mel_mutex_unlock(&dev->sampler_lock);
    return alive;
}

u32 mel_gpu_sampler_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    mel_assert(mel_gpu_sampler_alive(dev, sampler) && "sampler_bindless_slot: invalid sampler handle");
    mel_assert(dev->bindless.enabled && "bindless heap not enabled on this device");
    return sampler.slot.index;
}

// U11/U13: a pipeline with a static (immutable) sampler must keep that sampler alive for its lifetime
// (gpu-rhi.md §6.3 — the lifetime was previously the caller's unenforced contract). pipeline_create takes
// one claim per static sampler; pipeline_destroy releases it, so a user destroying their own handle does not
// free the VkSampler out from under a live pipeline.
bool mel_gpu__sampler_retain(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    mel_mutex_lock(&dev->sampler_lock);
    Mel_Gpu_Sampler_Obj* o = mel_gpu__table_get(dev, &dev->samplers, sampler.slot);
    if (o)
        o->refcount++;
    mel_mutex_unlock(&dev->sampler_lock);
    return o != NULL;
}

bool mel_gpu__sampler_get(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler, VkSampler* out)
{
    mel_mutex_lock(&dev->sampler_lock);
    Mel_Gpu_Sampler_Obj* o = mel_gpu__table_get(dev, &dev->samplers, sampler.slot);
    if (o)
        *out = o->sampler;
    mel_mutex_unlock(&dev->sampler_lock);
    return o != NULL;
}
