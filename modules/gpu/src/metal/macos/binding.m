#include "mtl_backend.h"

#include <slang/compile.h>

#include <log/log.h>

static u32 mel_gpu__min_u32(u32 a, u32 b) { return a < b ? a : b; }
static u32 mel_gpu__max_u32(u32 a, u32 b) { return a > b ? a : b; }

static const char* mel_gpu__class_name[MEL_GPU_BINDLESS_BINDING_COUNT] = {
    [MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE] = "sampled-image",
    [MEL_GPU_BINDLESS_BINDING_SAMPLER] = "sampler",
    [MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER] = "storage-buffer",
    [MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER] = "uniform-buffer",
    [MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE] = "storage-image",
};

static u32 mel_gpu__heap_cap_for_class(Mel_Gpu_Device* dev, u32 binding_class)
{
    if (binding_class >= MEL_GPU_BINDLESS_BINDING_COUNT)
        return 0;
    return dev->bindless.caps[binding_class];
}

void mel_gpu__bindless_init(Mel_Gpu_Device* dev, bool want)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    *b = (Mel_Gpu_Bindless){ 0 };

    if (!want || dev->caps.memory.bindless.tier == MEL_GPU_TIER_NONE)
        return;

    if (dev->mtl.argumentBuffersSupport < MTLArgumentBuffersTier2)
    {
        mel_log_error("gpu", "bindless: device '%s' reports argument-buffers tier 1; the device-global bindless heap requires tier 2; heap disabled", dev->caps.adapter.name);
        return;
    }
    bool residency_available = false;
    if (@available(macOS 15.0, iOS 18.0, *))
        residency_available = true;
    if (!residency_available)
    {
        mel_log_error("gpu", "bindless: MTLResidencySet (macOS 15 / iOS 18) unavailable; the device-global bindless heap requires it; heap disabled");
        return;
    }

    b->hw_max[MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE] = dev->caps.memory.bindless.max_texture_view_slots;
    b->hw_max[MEL_GPU_BINDLESS_BINDING_SAMPLER] = dev->caps.memory.bindless.max_sampler_slots;
    b->hw_max[MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER] = dev->caps.memory.bindless.max_storage_buffer_slots;
    b->hw_max[MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER] = dev->caps.memory.bindless.max_uniform_buffer_slots;
    b->hw_max[MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE] = dev->caps.memory.bindless.max_storage_image_slots;

    const u32 SEED_IMAGE = 1024, SEED_SAMPLER = 256, SEED_BUFFER = 1024;
    b->caps[MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE] = mel_gpu__min_u32(SEED_IMAGE, b->hw_max[MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE]);
    b->caps[MEL_GPU_BINDLESS_BINDING_SAMPLER] = mel_gpu__min_u32(SEED_SAMPLER, b->hw_max[MEL_GPU_BINDLESS_BINDING_SAMPLER]);
    b->caps[MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER] = mel_gpu__min_u32(SEED_BUFFER, b->hw_max[MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER]);
    b->caps[MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER] = mel_gpu__min_u32(SEED_BUFFER, b->hw_max[MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER]);
    b->caps[MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE] = mel_gpu__min_u32(SEED_IMAGE, b->hw_max[MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE]);

    for (u32 i = 0; i < MEL_GPU_BINDLESS_BINDING_COUNT; i++)
    {
        usize         bytes = (usize)b->caps[i] * sizeof(uint64_t);
        id<MTLBuffer> heap = [dev->mtl newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        if (!heap)
        {
            mel_log_error("gpu", "bindless: newBufferWithLength %zu for heap class %u failed; heap disabled", bytes, i);
            for (u32 j = 0; j < i; j++)
            {
                id h = (__bridge_transfer id)b->heaps[j];
                b->heaps[j] = NULL;
                (void)h;
                if (b->resources[j])
                    mel_dealloc(dev->alloc, b->resources[j]);
            }
            *b = (Mel_Gpu_Bindless){ 0 };
            return;
        }
        memset(heap.contents, 0, bytes);
        b->heaps[i] = (__bridge_retained void*)heap;
        b->resources[i] = mel_calloc(dev->alloc, (usize)b->caps[i] * sizeof(void*));
    }

    MTLResidencySetDescriptor* rsd = [[MTLResidencySetDescriptor alloc] init];
    rsd.label = @"mel-bindless-residency";
    NSError*            err = nil;
    id<MTLResidencySet> rs = [dev->mtl newResidencySetWithDescriptor:rsd error:&err];
    if (!rs)
    {
        mel_log_error("gpu", "bindless: newResidencySet failed: %s; heap disabled", err ? err.localizedDescription.UTF8String : "(no error)");
        for (u32 j = 0; j < MEL_GPU_BINDLESS_BINDING_COUNT; j++)
        {
            id h = (__bridge_transfer id)b->heaps[j];
            b->heaps[j] = NULL;
            (void)h;
            if (b->resources[j])
                mel_dealloc(dev->alloc, b->resources[j]);
        }
        *b = (Mel_Gpu_Bindless){ 0 };
        return;
    }
    b->residency = (__bridge_retained void*)rs;
    [dev->queue addResidencySet:rs];

    mel_mutex_init(&b->lock, MEL_MUTEX_PLAIN);
    b->enabled = true;

    dev->caps.memory.bindless.growable = true;
    dev->caps.memory.bindless.seed_texture_view_slots = b->caps[MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE];
    dev->caps.memory.bindless.seed_sampler_slots = b->caps[MEL_GPU_BINDLESS_BINDING_SAMPLER];
    dev->caps.memory.bindless.seed_storage_buffer_slots = b->caps[MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER];
    dev->caps.memory.bindless.seed_uniform_buffer_slots = b->caps[MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER];
    dev->caps.memory.bindless.seed_storage_image_slots = b->caps[MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE];

    mel_log_info("gpu", "bindless heap: per-class growable, seeds %u/%u/%u/%u/%u, walls %u/%u/%u/%u/%u (argument-buffer tier 2 + MTLResidencySet)",
                 b->caps[0], b->caps[1], b->caps[2], b->caps[3], b->caps[4],
                 b->hw_max[0], b->hw_max[1], b->hw_max[2], b->hw_max[3], b->hw_max[4]);
}

static void mel_gpu__bindless_drain_locked(Mel_Gpu_Device* dev, u64 watermark)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    u32               keep = 0;
    for (u32 i = 0; i < b->deferred_count; i++)
    {
        Mel_Gpu_Mtl_Bindless_Deferred* e = &b->deferred[i];
        if (e->marker > watermark)
        {
            b->deferred[keep++] = *e;
            continue;
        }
        if (e->remove_residency)
        {
            if (@available(macOS 15.0, iOS 18.0, *))
            {
                id<MTLResidencySet> rs = (__bridge id<MTLResidencySet>)b->residency;
                [rs removeAllocation:(__bridge id<MTLAllocation>)e->res];
                b->residency_dirty = true;
            }
        }
        id r = (__bridge_transfer id)e->res;
        e->res = NULL;
        (void)r;
    }
    b->deferred_count = keep;
}

void mel_gpu__bindless_drain(Mel_Gpu_Device* dev, u64 watermark)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled)
        return;
    mel_mutex_lock(&b->lock);
    if (b->deferred_count)
        mel_gpu__bindless_drain_locked(dev, watermark);
    mel_mutex_unlock(&b->lock);
}

void mel_gpu__bindless_shutdown(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled)
        return;
    mel_gpu__bindless_drain_locked(dev, UINT64_MAX);
    if (b->deferred)
        mel_dealloc(dev->alloc, b->deferred);
    if (@available(macOS 15.0, iOS 18.0, *))
    {
        id<MTLResidencySet> rs = (__bridge id<MTLResidencySet>)b->residency;
        if (rs)
            [dev->queue removeResidencySet:rs];
    }
    if (b->residency)
    {
        id rs = (__bridge_transfer id)b->residency;
        b->residency = NULL;
        (void)rs;
    }
    for (u32 i = 0; i < MEL_GPU_BINDLESS_BINDING_COUNT; i++)
    {
        if (b->resources[i])
        {
            for (u32 s = 0; s < b->caps[i]; s++)
            {
                if (b->resources[i][s])
                {
                    id r = (__bridge_transfer id)b->resources[i][s];
                    b->resources[i][s] = NULL;
                    (void)r;
                }
            }
            mel_dealloc(dev->alloc, b->resources[i]);
            b->resources[i] = NULL;
        }
        if (b->heaps[i])
        {
            id h = (__bridge_transfer id)b->heaps[i];
            b->heaps[i] = NULL;
            (void)h;
        }
    }
    mel_mutex_destroy(&b->lock);
    b->enabled = false;
}

bool mel_gpu__bindless_slot_fits(Mel_Gpu_Device* dev, u32 binding_class, u32 slot)
{
    if (!dev->bindless.enabled)
        return true;
    if (binding_class >= MEL_GPU_BINDLESS_BINDING_COUNT)
        return false;
    return slot < dev->bindless.hw_max[binding_class];
}

static bool mel_gpu__bindless_grow(Mel_Gpu_Device* dev, u32 cls, u32 need_slot)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    u32               old_cap = b->caps[cls];
    u32               new_cap = mel_gpu__min_u32(mel_gpu__max_u32(old_cap * 2, need_slot + 1), b->hw_max[cls]);

    usize         bytes = (usize)new_cap * sizeof(uint64_t);
    id<MTLBuffer> fresh = [dev->mtl newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (!fresh)
    {
        mel_log_error("gpu", "bindless: newBufferWithLength %zu failed growing the %s class %u -> %u", bytes, mel_gpu__class_name[cls], old_cap, new_cap);
        return false;
    }
    id<MTLBuffer> old = (__bridge_transfer id<MTLBuffer>)b->heaps[cls];
    memcpy(fresh.contents, old.contents, (usize)old_cap * sizeof(uint64_t));
    memset((u8*)fresh.contents + (usize)old_cap * sizeof(uint64_t), 0, bytes - (usize)old_cap * sizeof(uint64_t));
    b->heaps[cls] = (__bridge_retained void*)fresh;
    (void)old;

    void** grown = mel_calloc(dev->alloc, (usize)new_cap * sizeof(void*));
    memcpy(grown, b->resources[cls], (usize)old_cap * sizeof(void*));
    mel_dealloc(dev->alloc, b->resources[cls]);
    b->resources[cls] = grown;
    b->caps[cls] = new_cap;

    mel_log_warn("gpu", "bindless: %s heap grew %u -> %u", mel_gpu__class_name[cls], old_cap, new_cap);
    return true;
}

static bool mel_gpu__bindless_ensure_locked(Mel_Gpu_Device* dev, u32 cls, u32 slot)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (slot < b->caps[cls])
        return true;
    if (slot >= b->hw_max[cls] || !mel_gpu__bindless_grow(dev, cls, slot))
    {
        mel_log_error("gpu", "bindless: %s slot %u exceeds the device wall %u (BindlessSlotExhausted); registration refused", mel_gpu__class_name[cls], slot, b->hw_max[cls]);
        return false;
    }
    return true;
}

static void mel_gpu__bindless_make_resident(Mel_Gpu_Device* dev, id<MTLResource> res)
{
    if (@available(macOS 15.0, iOS 18.0, *))
    {
        id<MTLResidencySet> rs = (__bridge id<MTLResidencySet>)dev->bindless.residency;
        [rs addAllocation:res];
        dev->bindless.residency_dirty = true;
    }
}

void mel_gpu__bindless_residency_flush(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled)
        return;
    mel_mutex_lock(&b->lock);
    if (b->residency_dirty)
    {
        if (@available(macOS 15.0, iOS 18.0, *))
        {
            id<MTLResidencySet> rs = (__bridge id<MTLResidencySet>)b->residency;
            [rs commit];
            [rs requestResidency];
        }
        b->residency_dirty = false;
    }
    mel_mutex_unlock(&b->lock);
}

static void mel_gpu__bindless_write_id(Mel_Gpu_Device* dev, u32 binding_class, u32 slot, MTLResourceID rid)
{
    id<MTLBuffer>  heap = (__bridge id<MTLBuffer>)dev->bindless.heaps[binding_class];
    MTLResourceID* table = (MTLResourceID*)heap.contents;
    table[slot] = rid;
}

static void mel_gpu__bindless_write_address(Mel_Gpu_Device* dev, u32 binding_class, u32 slot, uint64_t address)
{
    id<MTLBuffer> heap = (__bridge id<MTLBuffer>)dev->bindless.heaps[binding_class];
    uint64_t*     table = (uint64_t*)heap.contents;
    table[slot] = address;
}

static void mel_gpu__bindless_defer_release_locked(Mel_Gpu_Device* dev, void* res, bool remove_residency)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (b->deferred_count == b->deferred_cap)
    {
        u32 cap = b->deferred_cap ? b->deferred_cap * 2 : 16;
        b->deferred = b->deferred ? mel_realloc(dev->alloc, b->deferred, sizeof(*b->deferred) * cap) : mel_alloc(dev->alloc, sizeof(*b->deferred) * cap);
        b->deferred_cap = cap;
    }
    b->deferred[b->deferred_count++] = (Mel_Gpu_Mtl_Bindless_Deferred){ .marker = dev->submit_serial, .res = res, .remove_residency = remove_residency };
}

/* Mirror the slot->resource registration (alongside the #34 gpuResourceID/gpuAddress heap
   write) so the from-slang Metal per-dispatch argument buffer can resolve slot -> live
   resource by id<MTLResource>. The argument encoder needs the object, not just its id. */
static void mel_gpu__bindless_store_resource(Mel_Gpu_Device* dev, u32 binding_class, u32 slot, id obj, bool in_residency)
{
    void** table = dev->bindless.resources[binding_class];
    if (!table)
        return;
    if (table[slot])
    {
        mel_gpu__bindless_defer_release_locked(dev, table[slot], in_residency);
        table[slot] = NULL;
    }
    table[slot] = (__bridge_retained void*)obj;
}

void mel_gpu__bindless_unregister(Mel_Gpu_Device* dev, u32 binding_class, u32 slot)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled || binding_class >= MEL_GPU_BINDLESS_BINDING_COUNT)
        return;
    mel_mutex_lock(&b->lock);
    void** table = b->resources[binding_class];
    if (table && slot < b->caps[binding_class] && table[slot])
    {
        mel_gpu__bindless_defer_release_locked(dev, table[slot], binding_class != MEL_GPU_BINDLESS_BINDING_SAMPLER);
        table[slot] = NULL;
    }
    mel_mutex_unlock(&b->lock);
}

id<MTLResource> mel_gpu__bindless_resource(Mel_Gpu_Device* dev, u32 binding_class, u32 slot)
{
    if (binding_class >= MEL_GPU_BINDLESS_BINDING_COUNT)
        return nil;
    void** table = dev->bindless.resources[binding_class];
    if (!table || slot >= dev->bindless.caps[binding_class])
        return nil;
    return (__bridge id<MTLResource>)table[slot];
}

u32 mel_gpu__bindless_class_of_slang_kind(u32 slang_resource_kind)
{
    switch (slang_resource_kind)
    {
    case MEL_SLANG_RESOURCE_SAMPLED_TEXTURE:
        return MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE;
    case MEL_SLANG_RESOURCE_STORAGE_TEXTURE:
        return MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE;
    case MEL_SLANG_RESOURCE_STORAGE_BUFFER:
        return MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER;
    case MEL_SLANG_RESOURCE_UNIFORM_BUFFER:
        return MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER;
    case MEL_SLANG_RESOURCE_SAMPLER:
        return MEL_GPU_BINDLESS_BINDING_SAMPLER;
    default:
        return MEL_GPU_BINDLESS_BINDING_COUNT;
    }
}

void mel_gpu__bindless_register_sampled_image(Mel_Gpu_Device* dev, u32 slot, id<MTLTexture> view)
{
    if (!dev->bindless.enabled)
        return;
    mel_mutex_lock(&dev->bindless.lock);
    if (!mel_gpu__bindless_ensure_locked(dev, MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, slot))
    {
        mel_mutex_unlock(&dev->bindless.lock);
        return;
    }
    mel_gpu__bindless_write_id(dev, MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, slot, view.gpuResourceID);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, slot, view, true);
    mel_gpu__bindless_make_resident(dev, view);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 slot, id<MTLTexture> view)
{
    if (!dev->bindless.enabled)
        return;
    mel_mutex_lock(&dev->bindless.lock);
    if (!mel_gpu__bindless_ensure_locked(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, slot))
    {
        mel_mutex_unlock(&dev->bindless.lock);
        return;
    }
    mel_gpu__bindless_write_id(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, slot, view.gpuResourceID);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, slot, view, true);
    mel_gpu__bindless_make_resident(dev, view);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_storage_buffer(Mel_Gpu_Device* dev, u32 slot, id<MTLBuffer> buf)
{
    if (!dev->bindless.enabled)
        return;
    mel_mutex_lock(&dev->bindless.lock);
    if (!mel_gpu__bindless_ensure_locked(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER, slot))
    {
        mel_mutex_unlock(&dev->bindless.lock);
        return;
    }
    mel_gpu__bindless_write_address(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER, slot, buf.gpuAddress);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER, slot, buf, true);
    mel_gpu__bindless_make_resident(dev, buf);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_uniform_buffer(Mel_Gpu_Device* dev, u32 slot, id<MTLBuffer> buf)
{
    if (!dev->bindless.enabled)
        return;
    mel_mutex_lock(&dev->bindless.lock);
    if (!mel_gpu__bindless_ensure_locked(dev, MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER, slot))
    {
        mel_mutex_unlock(&dev->bindless.lock);
        return;
    }
    mel_gpu__bindless_write_address(dev, MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER, slot, buf.gpuAddress);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER, slot, buf, true);
    mel_gpu__bindless_make_resident(dev, buf);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 slot, id<MTLSamplerState> sampler)
{
    if (!dev->bindless.enabled)
        return;
    mel_mutex_lock(&dev->bindless.lock);
    if (!mel_gpu__bindless_ensure_locked(dev, MEL_GPU_BINDLESS_BINDING_SAMPLER, slot))
    {
        mel_mutex_unlock(&dev->bindless.lock);
        return;
    }
    mel_gpu__bindless_write_id(dev, MEL_GPU_BINDLESS_BINDING_SAMPLER, slot, sampler.gpuResourceID);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_SAMPLER, slot, sampler, false);
    mel_mutex_unlock(&dev->bindless.lock);
}

id<MTLSamplerState> mel_gpu__bindless_sampler(Mel_Gpu_Device* dev, u32 slot)
{
    void** table = dev->bindless.resources[MEL_GPU_BINDLESS_BINDING_SAMPLER];
    if (!table || slot >= dev->bindless.caps[MEL_GPU_BINDLESS_BINDING_SAMPLER])
        return nil;
    return (__bridge id<MTLSamplerState>)table[slot];
}

void mel_gpu__bindless_bind_render(Mel_Gpu_Device* dev, id<MTLRenderCommandEncoder> enc)
{
    mel_mutex_lock(&dev->bindless.lock);
    for (u32 i = 0; i < MEL_GPU_BINDLESS_BINDING_COUNT; i++)
    {
        id<MTLBuffer> heap = (__bridge id<MTLBuffer>)dev->bindless.heaps[i];
        u32           index = MEL_GPU_METAL_BINDLESS_HEAP_INDEX(i);
        [enc setVertexBuffer:heap offset:0 atIndex:index];
        [enc setFragmentBuffer:heap offset:0 atIndex:index];
    }
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_bind_compute(Mel_Gpu_Device* dev, id<MTLComputeCommandEncoder> enc)
{
    mel_mutex_lock(&dev->bindless.lock);
    for (u32 i = 0; i < MEL_GPU_BINDLESS_BINDING_COUNT; i++)
    {
        id<MTLBuffer> heap = (__bridge id<MTLBuffer>)dev->bindless.heaps[i];
        [enc setBuffer:heap offset:0 atIndex:MEL_GPU_METAL_BINDLESS_HEAP_INDEX(i)];
    }
    mel_mutex_unlock(&dev->bindless.lock);
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

u32 mel_gpu_sampler_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    mel_assert(dev && mel_gpu_sampler_alive(dev, sampler) && "sampler_bindless_slot: invalid sampler handle");
    mel_assert(dev->bindless.enabled && "bindless heap not enabled on this device");
    return sampler.slot.index;
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
    if (cmd->compute_encoder)
    {
        mel_gpu__bindless_bind_compute(dev, cmd->compute_encoder);
        return;
    }
    if (cmd->encoder)
    {
        mel_gpu__bindless_bind_render(dev, cmd->encoder);
        return;
    }
    mel_assert(!"cmd_bind_bindless: no active render or compute encoder (bind a pipeline / begin a pass first)");
}
