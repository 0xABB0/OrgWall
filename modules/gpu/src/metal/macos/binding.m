#include "mtl_backend.h"

#include <slang/compile.h>

#include <log/log.h>

static u32 mel_gpu__min_u32(u32 a, u32 b) { return a < b ? a : b; }

static u32 mel_gpu__heap_cap_for_class(Mel_Gpu_Device* dev, u32 binding_class)
{
    switch (binding_class)
    {
    case MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE:
        return dev->bindless.cap_sampled_image;
    case MEL_GPU_BINDLESS_BINDING_SAMPLER:
        return dev->bindless.cap_sampler;
    case MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER:
        return dev->bindless.cap_storage_buffer;
    case MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER:
        return dev->bindless.cap_uniform_buffer;
    case MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE:
        return dev->bindless.cap_storage_image;
    default:
        return 0;
    }
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

    const u32 DEFAULT_IMAGE = 16384, DEFAULT_SAMPLER = 2048, DEFAULT_BUFFER = 16384;
    b->cap_sampled_image = mel_gpu__min_u32(DEFAULT_IMAGE, dev->caps.memory.bindless.max_texture_view_slots);
    b->cap_sampler = mel_gpu__min_u32(DEFAULT_SAMPLER, dev->caps.memory.bindless.max_sampler_slots);
    b->cap_storage_buffer = mel_gpu__min_u32(DEFAULT_BUFFER, dev->caps.memory.bindless.max_storage_buffer_slots);
    b->cap_uniform_buffer = mel_gpu__min_u32(DEFAULT_BUFFER, dev->caps.memory.bindless.max_uniform_buffer_slots);
    b->cap_storage_image = mel_gpu__min_u32(DEFAULT_IMAGE, dev->caps.memory.bindless.max_storage_image_slots);

    const u32 caps[MEL_GPU_BINDLESS_BINDING_COUNT] = {
        b->cap_sampled_image, b->cap_sampler, b->cap_storage_buffer, b->cap_uniform_buffer, b->cap_storage_image,
    };

    for (u32 i = 0; i < MEL_GPU_BINDLESS_BINDING_COUNT; i++)
    {
        usize         bytes = (usize)caps[i] * sizeof(uint64_t);
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
        b->caps[i] = caps[i];
        b->resources[i] = mel_calloc(dev->alloc, (usize)caps[i] * sizeof(void*));
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

    dev->caps.memory.bindless.max_texture_view_slots = b->cap_sampled_image;
    dev->caps.memory.bindless.max_sampler_slots = b->cap_sampler;
    dev->caps.memory.bindless.max_storage_buffer_slots = b->cap_storage_buffer;
    dev->caps.memory.bindless.max_uniform_buffer_slots = b->cap_uniform_buffer;
    dev->caps.memory.bindless.max_storage_image_slots = b->cap_storage_image;

    mel_log_info("gpu", "bindless heap: %u sampled images, %u samplers, %u storage buffers (argument-buffer tier 2 + MTLResidencySet)", b->cap_sampled_image, b->cap_sampler, b->cap_storage_buffer);
}

void mel_gpu__bindless_shutdown(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Bindless* b = &dev->bindless;
    if (!b->enabled)
        return;
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
    return slot < mel_gpu__heap_cap_for_class(dev, binding_class);
}

static bool mel_gpu__bindless_check(Mel_Gpu_Device* dev, u32 slot, u32 binding_class, const char* klass)
{
    if (!dev->bindless.enabled)
        return false;
    u32 cap = mel_gpu__heap_cap_for_class(dev, binding_class);
    if (slot >= cap)
    {
        mel_log_error("gpu", "bindless: %s slot %u exceeds heap capacity %u (BindlessSlotExhausted); registration refused", klass, slot, cap);
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
        [rs commit];
        [rs requestResidency];
    }
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

/* Mirror the slot->resource registration (alongside the #34 gpuResourceID/gpuAddress heap
   write) so the from-slang Metal per-dispatch argument buffer can resolve slot -> live
   resource by id<MTLResource>. The argument encoder needs the object, not just its id. */
static void mel_gpu__bindless_store_resource(Mel_Gpu_Device* dev, u32 binding_class, u32 slot, id<MTLResource> res)
{
    void** table = dev->bindless.resources[binding_class];
    if (!table)
        return;
    if (table[slot])
    {
        id prev = (__bridge_transfer id)table[slot];
        table[slot] = NULL;
        (void)prev;
    }
    table[slot] = (__bridge_retained void*)res;
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
    if (!mel_gpu__bindless_check(dev, slot, MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, "sampled-image"))
        return;
    mel_mutex_lock(&dev->bindless.lock);
    mel_gpu__bindless_write_id(dev, MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, slot, view.gpuResourceID);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, slot, view);
    mel_gpu__bindless_make_resident(dev, view);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 slot, id<MTLTexture> view)
{
    if (!mel_gpu__bindless_check(dev, slot, MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, "storage-image"))
        return;
    mel_mutex_lock(&dev->bindless.lock);
    mel_gpu__bindless_write_id(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, slot, view.gpuResourceID);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, slot, view);
    mel_gpu__bindless_make_resident(dev, view);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_storage_buffer(Mel_Gpu_Device* dev, u32 slot, id<MTLBuffer> buf)
{
    if (!mel_gpu__bindless_check(dev, slot, MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER, "storage-buffer"))
        return;
    mel_mutex_lock(&dev->bindless.lock);
    mel_gpu__bindless_write_address(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER, slot, buf.gpuAddress);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER, slot, buf);
    mel_gpu__bindless_make_resident(dev, buf);
    mel_mutex_unlock(&dev->bindless.lock);
}

void mel_gpu__bindless_register_uniform_buffer(Mel_Gpu_Device* dev, u32 slot, id<MTLBuffer> buf)
{
    if (!mel_gpu__bindless_check(dev, slot, MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER, "uniform-buffer"))
        return;
    mel_mutex_lock(&dev->bindless.lock);
    mel_gpu__bindless_write_address(dev, MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER, slot, buf.gpuAddress);
    mel_gpu__bindless_store_resource(dev, MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER, slot, buf);
    mel_gpu__bindless_make_resident(dev, buf);
    mel_mutex_unlock(&dev->bindless.lock);
}

static void mel_gpu__bindless_store_object(Mel_Gpu_Device* dev, u32 binding_class, u32 slot, id obj)
{
    void** table = dev->bindless.resources[binding_class];
    if (!table)
        return;
    if (table[slot])
    {
        id prev = (__bridge_transfer id)table[slot];
        table[slot] = NULL;
        (void)prev;
    }
    table[slot] = (__bridge_retained void*)obj;
}

void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 slot, id<MTLSamplerState> sampler)
{
    if (!mel_gpu__bindless_check(dev, slot, MEL_GPU_BINDLESS_BINDING_SAMPLER, "sampler"))
        return;
    mel_mutex_lock(&dev->bindless.lock);
    mel_gpu__bindless_write_id(dev, MEL_GPU_BINDLESS_BINDING_SAMPLER, slot, sampler.gpuResourceID);
    mel_gpu__bindless_store_object(dev, MEL_GPU_BINDLESS_BINDING_SAMPLER, slot, sampler);
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
    for (u32 i = 0; i < MEL_GPU_BINDLESS_BINDING_COUNT; i++)
    {
        id<MTLBuffer> heap = (__bridge id<MTLBuffer>)dev->bindless.heaps[i];
        u32           index = MEL_GPU_METAL_BINDLESS_HEAP_INDEX(i);
        [enc setVertexBuffer:heap offset:0 atIndex:index];
        [enc setFragmentBuffer:heap offset:0 atIndex:index];
    }
}

void mel_gpu__bindless_bind_compute(Mel_Gpu_Device* dev, id<MTLComputeCommandEncoder> enc)
{
    for (u32 i = 0; i < MEL_GPU_BINDLESS_BINDING_COUNT; i++)
    {
        id<MTLBuffer> heap = (__bridge id<MTLBuffer>)dev->bindless.heaps[i];
        [enc setBuffer:heap offset:0 atIndex:MEL_GPU_METAL_BINDLESS_HEAP_INDEX(i)];
    }
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
