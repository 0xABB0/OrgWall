#include "d3d_backend.h"

#include <gpu/sampler.h>
#include <log/log.h>

#include <string.h>

// U11 samplers (gpu-rhi.md §6.3). A D3D12 sampler is a heap descriptor, not a COM object. Auto-dedup by
// canonical key (the public contract): identical descriptors share one handle + one sampler-heap slot,
// refcounted; the slot retires at refcount 0 (future-gated). slot == handle.index in the sampler heap
// (its own index space — samplers never collide with CBV/SRV/UAV resources, §6.7).
//
// The intern table and the sampler slotmap are both guarded by dev->obj_lock; this file touches the slotmap
// directly (mel_slotmap_*) rather than via the mel_gpu__table_* wrappers, which lock obj_lock themselves —
// nesting a PLAIN mutex would deadlock.

static u64 mel_gpu__fnv1a(const void* p, usize n)
{
    u64       h = 1469598103934665603ULL;
    const u8* b = p;
    for (usize i = 0; i < n; i++)
    {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static D3D12_TEXTURE_ADDRESS_MODE mel_gpu__address(Mel_Gpu_Wrap w)
{
    switch (w)
    {
    case MEL_GPU_WRAP_MIRROR_REPEAT:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case MEL_GPU_WRAP_CLAMP_EDGE:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case MEL_GPU_WRAP_CLAMP_BORDER:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case MEL_GPU_WRAP_REPEAT:
    default:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

static D3D12_COMPARISON_FUNC mel_gpu__compare(Mel_Gpu_Compare_Op c)
{
    switch (c)
    {
    case MEL_GPU_COMPARE_NEVER:
        return D3D12_COMPARISON_FUNC_NEVER;
    case MEL_GPU_COMPARE_LESS:
        return D3D12_COMPARISON_FUNC_LESS;
    case MEL_GPU_COMPARE_EQUAL:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case MEL_GPU_COMPARE_LESS_EQUAL:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case MEL_GPU_COMPARE_GREATER:
        return D3D12_COMPARISON_FUNC_GREATER;
    case MEL_GPU_COMPARE_NOT_EQUAL:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case MEL_GPU_COMPARE_GREATER_EQUAL:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case MEL_GPU_COMPARE_ALWAYS:
    case MEL_GPU_COMPARE_NONE:
    default:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

// D3D12_FILTER is a packed enum: bit4 = min-linear, bit2 = mag-linear, bit0 = mip-linear; 0x55 = anisotropic;
// +0x80 selects the comparison-reduction variant. (D3D12_FILTER_MIN_MAG_MIP_LINEAR == 0x15, etc.)
static D3D12_FILTER mel_gpu__filter(Mel_Gpu_Filter mn, Mel_Gpu_Filter mg, Mel_Gpu_Mipmap_Mode mip, bool aniso, bool compare)
{
    UINT f;
    if (aniso)
        f = 0x55;
    else
        f = (mn == MEL_GPU_FILTER_LINEAR ? 0x10u : 0u) | (mg == MEL_GPU_FILTER_LINEAR ? 0x4u : 0u) | (mip == MEL_GPU_MIPMAP_LINEAR ? 0x1u : 0u);
    if (compare)
        f |= 0x80u;
    return (D3D12_FILTER)f;
}

static void mel_gpu__border(Mel_Gpu_Border_Color b, float out[4])
{
    switch (b)
    {
    case MEL_GPU_BORDER_OPAQUE_BLACK:
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        break;
    case MEL_GPU_BORDER_OPAQUE_WHITE:
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        break;
    case MEL_GPU_BORDER_TRANSPARENT_BLACK:
    default:
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        break;
    }
}

Mel_Gpu_Sampler_Create_Result mel_gpu_sampler_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt)
{
    Mel_Gpu_Sampler_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SAMPLER_CREATE_OK };
    if (!dev)
    {
        res.status = MEL_GPU_SAMPLER_CREATE_BAD_PARAMS;
        return res;
    }

    f32  max_aniso = dev->caps.sampler.max_anisotropy > 1.0f ? dev->caps.sampler.max_anisotropy : 1.0f;
    f32  aniso = opt.max_anisotropy;
    if (aniso > max_aniso)
        aniso = max_aniso;
    bool use_aniso = aniso > 1.0f && dev->caps.sampler.anisotropy;
    bool compare = opt.compare != MEL_GPU_COMPARE_NONE;

    Mel_Gpu_Sampler_Key key = {
        .min_filter = (u8)opt.min_filter,
        .mag_filter = (u8)opt.mag_filter,
        .mip_filter = (u8)opt.mip_filter,
        .wrap_u = (u8)opt.wrap_u,
        .wrap_v = (u8)opt.wrap_v,
        .wrap_w = (u8)opt.wrap_w,
        .compare = (u8)opt.compare,
        .border = (u8)opt.border,
        .max_anisotropy = use_aniso ? aniso : 1.0f,
        .lod_min = opt.lod_min,
        .lod_max = opt.lod_max,
    };
    u64 hash = mel_gpu__fnv1a(&key, sizeof key);

    D3D12_SAMPLER_DESC desc = {
        .Filter = mel_gpu__filter(opt.min_filter, opt.mag_filter, opt.mip_filter, use_aniso, compare),
        .AddressU = mel_gpu__address(opt.wrap_u),
        .AddressV = mel_gpu__address(opt.wrap_v),
        .AddressW = mel_gpu__address(opt.wrap_w),
        .MipLODBias = 0.0f,
        .MaxAnisotropy = use_aniso ? (UINT)aniso : 1,
        .ComparisonFunc = mel_gpu__compare(opt.compare),
        .MinLOD = opt.lod_min,
        .MaxLOD = opt.lod_max > 0.0f ? opt.lod_max : D3D12_FLOAT32_MAX,
    };
    mel_gpu__border(opt.border, desc.BorderColor);

    mel_mutex_lock(&dev->obj_lock);
    for (u32 i = 0; i < dev->sampler_intern_count; i++)
    {
        if (dev->sampler_interns[i].hash != hash)
            continue;
        Mel_Gpu_Sampler_Obj* o = mel_slotmap_get(&dev->samplers.map, dev->sampler_interns[i].handle);
        if (o && memcmp(&o->key, &key, sizeof key) == 0)
        {
            o->refcount++;
            res.value.slot = dev->sampler_interns[i].handle;
            mel_mutex_unlock(&dev->obj_lock);
            return res; // dedup hit: the descriptor is already in the heap at this slot
        }
    }

    Mel_Gpu_Sampler_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.desc = desc;
    obj.key = key;
    obj.hash = hash;
    obj.refcount = 1;
    Mel_SlotMap_Handle h = mel_slotmap_insert(&dev->samplers.map, &obj);

    if (dev->sampler_intern_count == dev->sampler_intern_cap)
    {
        u32 cap = dev->sampler_intern_cap ? dev->sampler_intern_cap * 2 : 16;
        dev->sampler_interns = dev->sampler_interns ? mel_realloc(dev->alloc, dev->sampler_interns, sizeof(Mel_Gpu_Sampler_Intern) * cap) : mel_alloc(dev->alloc, sizeof(Mel_Gpu_Sampler_Intern) * cap);
        dev->sampler_intern_cap = cap;
    }
    dev->sampler_interns[dev->sampler_intern_count++] = (Mel_Gpu_Sampler_Intern){ .hash = hash, .handle = h };
    mel_mutex_unlock(&dev->obj_lock);

    if (dev->bindless_enabled)
        mel_gpu__bindless_register_sampler(dev, h.index, &desc);

    res.value.slot = h;
    return res;
}

void mel_gpu_sampler_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    mel_mutex_lock(&dev->obj_lock);
    Mel_Gpu_Sampler_Obj* o = mel_slotmap_get(&dev->samplers.map, sampler.slot);
    if (!o)
    {
        mel_mutex_unlock(&dev->obj_lock);
        return;
    }
    if (--o->refcount > 0)
    {
        mel_mutex_unlock(&dev->obj_lock);
        return; // a logical claim remains (dedup share / static-sampler retain)
    }
    for (u32 i = 0; i < dev->sampler_intern_count; i++)
        if (mel_gpu_handle_eq(dev->sampler_interns[i].handle, sampler.slot))
        {
            dev->sampler_interns[i] = dev->sampler_interns[--dev->sampler_intern_count];
            break;
        }
    // Roll the generation now (use-after-free stays loud); reclaim the slot once submissions retire (§3.3),
    // so the sampler-heap descriptor is never overwritten while an in-flight draw still samples it.
    mel_slotmap_remove_deferred(&dev->samplers.map, sampler.slot);
    mel_mutex_unlock(&dev->obj_lock);
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .reclaim_table = &dev->samplers, .reclaim_index = sampler.slot.index, .has_reclaim = true });
}

bool mel_gpu_sampler_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler) { return mel_gpu__table_get(dev, &dev->samplers, sampler.slot) != NULL; }

u32 mel_gpu_sampler_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    mel_assert(dev->bindless_enabled);
    mel_assert(mel_gpu_sampler_alive(dev, sampler));
    return sampler.slot.index; // the sampler heap is its own index space (§3.1)
}

void mel_gpu__sampler_retain(Mel_Gpu_Device* dev, Mel_Gpu_Sampler s)
{
    mel_mutex_lock(&dev->obj_lock);
    Mel_Gpu_Sampler_Obj* o = mel_slotmap_get(&dev->samplers.map, s.slot);
    if (o)
        o->refcount++;
    mel_mutex_unlock(&dev->obj_lock);
}

bool mel_gpu__sampler_desc(Mel_Gpu_Device* dev, Mel_Gpu_Sampler s, D3D12_SAMPLER_DESC* out)
{
    Mel_Gpu_Sampler_Obj* o = mel_gpu__table_get(dev, &dev->samplers, s.slot);
    if (!o)
        return false;
    *out = o->desc;
    return true;
}
