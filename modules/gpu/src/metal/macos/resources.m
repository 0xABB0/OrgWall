#include "mtl_backend.h"

#include <log/log.h>

bool mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, id<MTLBuffer>* out)
{
    Mel_Gpu_Buffer_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->buffers, buf.slot, &o))
        return false;
    *out = (__bridge id<MTLBuffer>)o.buf;
    return true;
}

bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj* out) { return mel_gpu__table_get_copy(dev, &dev->textures, tex.slot, out); }

bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj* out) { return mel_gpu__table_get_copy(dev, &dev->texture_views, view.slot, out); }

Mel_Gpu_Buffer_Create_Result mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    Mel_Gpu_Buffer_Create_Result res = { .status = MEL_GPU_BUFFER_CREATE_OK };
    if (!dev || opt.size == 0)
    {
        res.status = MEL_GPU_BUFFER_CREATE_BAD_PARAMS;
        mel_log_error("gpu", "buffer_create: null device or zero size");
        return res;
    }

    bool               host_visible = opt.memory == MEL_GPU_MEMORY_UPLOAD || opt.memory == MEL_GPU_MEMORY_READBACK;
    MTLResourceOptions ropt = host_visible ? MTLResourceStorageModeShared : MTLResourceStorageModePrivate;

    id<MTLBuffer> mb = nil;
    if (opt.data && host_visible)
        mb = [dev->mtl newBufferWithBytes:opt.data length:opt.size options:ropt];
    else
        mb = [dev->mtl newBufferWithLength:opt.size options:ropt];

    if (!mb)
    {
        res.status = MEL_GPU_BUFFER_CREATE_OOM;
        mel_log_error("gpu", "buffer_create: newBuffer returned nil for %zu bytes", opt.size);
        return res;
    }
    if (opt.data && !host_visible)
        mel_log_warn("gpu", "buffer_create: initial data ignored for device-local buffer '%s' (no staging-copy path this round)", opt.name ? opt.name : "(unnamed)");

    Mel_Gpu_Buffer_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .capture_replay = opt.capture_replay, .name = opt.name },
        .buf = (__bridge_retained void*)mb,
        .size = opt.size,
        .host_visible = host_visible,
        .usage = opt.usage,
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->buffers, &o);
    res.value = (Mel_Gpu_Buffer){ h };

    if (dev->bindless.enabled)
    {
        bool stor = (opt.usage & MEL_GPU_BUFFER_STORAGE) != 0;
        bool unif = (opt.usage & MEL_GPU_BUFFER_UNIFORM) != 0;
        bool fits = true;
        if (stor)
            fits = mel_gpu__bindless_slot_fits(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER, h.index) && fits;
        if (unif)
            fits = mel_gpu__bindless_slot_fits(dev, MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER, h.index) && fits;
        if (!fits)
        {
            mel_log_error("gpu", "buffer_create '%s': bindless slot %u exceeds a heap class cap (BindlessSlotExhausted)", opt.name ? opt.name : "(unnamed)", h.index);
            mel_gpu__table_remove(dev, &dev->buffers, h);
            id discard = (__bridge_transfer id)o.buf;
            (void)discard;
            res.value = (Mel_Gpu_Buffer){ mel_gpu_handle_null() };
            res.status = MEL_GPU_BUFFER_CREATE_BINDLESS_SLOT_EXHAUSTED;
            return res;
        }
        if (stor)
            mel_gpu__bindless_register_storage_buffer(dev, h.index, mb);
        if (unif)
            mel_gpu__bindless_register_uniform_buffer(dev, h.index, mb);
    }
    return res;
}

void mel_gpu_buffer_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o)
        return;
    if (o->buf)
    {
        id mb = (__bridge_transfer id)o->buf;
        o->buf = NULL;
        (void)mb;
    }
    mel_gpu__table_remove(dev, &dev->buffers, buf.slot);
}

bool mel_gpu_buffer_alive(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf) { return mel_gpu__table_alive(dev, &dev->buffers, buf.slot); }

void mel_gpu_buffer_write(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, const void* data, usize size)
{
    Mel_Gpu_Buffer_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->buffers, buf.slot, &o))
    {
        mel_log_error("gpu", "buffer_write: invalid buffer handle");
        return;
    }
    if (!o.host_visible)
    {
        mel_log_error("gpu", "buffer_write: buffer '%s' is device-local; no host-write path on the Metal backend this round", o.header.name ? o.header.name : "(unnamed)");
        return;
    }
    if (size > o.size)
        size = o.size;
    memcpy(((__bridge id<MTLBuffer>)o.buf).contents, data, size);
}

void* mel_gpu_buffer_mapped(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->buffers, buf.slot, &o))
        return NULL;
    if (!o.host_visible)
        return NULL;
    return ((__bridge id<MTLBuffer>)o.buf).contents;
}

u32 mel_gpu_buffer_make_resident(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    (void)dev;
    (void)buf;
    return 0;
}

u32 mel_gpu_buffer_evict(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    (void)dev;
    (void)buf;
    return 0;
}

Mel_Gpu_Buffer mel_gpu_buffer_import(Mel_Gpu_Device* dev, void* native_buffer, usize size, const char* name)
{
    (void)native_buffer;
    (void)size;
    (void)name;
    mel_log_error("gpu", "buffer_import: external-memory import is not implemented on the Metal backend this round");
    (void)dev;
    return (Mel_Gpu_Buffer){ mel_gpu_handle_null() };
}

static MTLTextureUsage mel_gpu__texture_usage(Mel_Gpu_Texture_Usage u)
{
    MTLTextureUsage out = 0;
    if (u & MEL_GPU_TEXTURE_SAMPLED)
        out |= MTLTextureUsageShaderRead;
    if (u & MEL_GPU_TEXTURE_STORAGE)
        out |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    if (u & MEL_GPU_TEXTURE_ATTACHMENT)
        out |= MTLTextureUsageRenderTarget;
    return out;
}

Mel_Gpu_Texture_Create_Result mel_gpu_texture_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Texture_Opt opt)
{
    Mel_Gpu_Texture_Create_Result res = { .status = MEL_GPU_TEXTURE_CREATE_OK };
    if (!dev || opt.extent.width == 0 || opt.extent.height == 0)
    {
        res.status = MEL_GPU_TEXTURE_CREATE_BAD_PARAMS;
        mel_log_error("gpu", "texture_create: null device or zero extent");
        return res;
    }

    MTLPixelFormat fmt = mel_gpu__mtl_format(opt.format);
    if (fmt == MTLPixelFormatInvalid)
    {
        res.status = MEL_GPU_TEXTURE_CREATE_BAD_PARAMS;
        mel_log_error("gpu", "texture_create: format %d has no Metal mapping", (int)opt.format);
        return res;
    }

    MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
    td.pixelFormat = fmt;
    td.width = opt.extent.width;
    td.height = opt.extent.height;
    td.depth = opt.kind == MEL_GPU_TEXTURE_3D ? (opt.extent.depth ? opt.extent.depth : 1) : 1;
    td.mipmapLevelCount = opt.mip_levels ? opt.mip_levels : 1;
    td.arrayLength = opt.array_layers ? opt.array_layers : 1;
    td.sampleCount = opt.sample_count ? opt.sample_count : 1;
    td.usage = mel_gpu__texture_usage(opt.usage);
    td.textureType = opt.kind == MEL_GPU_TEXTURE_3D ? MTLTextureType3D : (td.sampleCount > 1 ? MTLTextureType2DMultisample : MTLTextureType2D);
    td.storageMode = MTLStorageModePrivate;

    id<MTLTexture> mt = [dev->mtl newTextureWithDescriptor:td];
    if (!mt)
    {
        res.status = MEL_GPU_TEXTURE_CREATE_OOM;
        mel_log_error("gpu", "texture_create: newTexture returned nil (%ux%u)", opt.extent.width, opt.extent.height);
        return res;
    }

    Mel_Gpu_Texture_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .capture_replay = opt.capture_replay, .name = opt.name },
        .texture = (__bridge_retained void*)mt,
        .format = fmt,
        .aspect = mel_gpu_format_is_depth(opt.format) ? MEL_GPU_ASPECT_DEPTH : MEL_GPU_ASPECT_COLOR,
        .width = opt.extent.width,
        .height = opt.extent.height,
        .depth = (u32)td.depth,
        .mip_levels = (u32)td.mipmapLevelCount,
        .array_layers = (u32)td.arrayLength,
        .sample_count = (u32)td.sampleCount,
        .usage = opt.usage,
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->textures, &o);
    res.value = (Mel_Gpu_Texture){ h };
    return res;
}

void mel_gpu_texture_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex)
{
    Mel_Gpu_Texture_Obj* o = mel_gpu__table_get(dev, &dev->textures, tex.slot);
    if (!o)
        return;
    if (o->texture)
    {
        id mt = (__bridge_transfer id)o->texture;
        o->texture = NULL;
        (void)mt;
    }
    mel_gpu__table_remove(dev, &dev->textures, tex.slot);
}

bool mel_gpu_texture_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex) { return mel_gpu__table_alive(dev, &dev->textures, tex.slot); }

void mel_gpu_texture_write(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Region region, const void* data, usize bytes)
{
    (void)bytes;
    Mel_Gpu_Texture_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->textures, tex.slot, &o))
    {
        mel_assert(!"texture_write: invalid texture handle");
        return;
    }
    mel_log_error("gpu", "texture_write: device-local texture upload is not implemented on the Metal backend this round (texture '%s')", o.header.name ? o.header.name : "(unnamed)");
    (void)region;
    (void)data;
}

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_view_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View_Opt opt)
{
    Mel_Gpu_Texture_View_Create_Result res = { .status = MEL_GPU_TEXTURE_VIEW_CREATE_OK };
    Mel_Gpu_Texture_Obj                tex;
    if (!mel_gpu__table_get_copy(dev, &dev->textures, opt.texture.slot, &tex))
    {
        res.status = MEL_GPU_TEXTURE_VIEW_CREATE_BAD_TEXTURE;
        mel_log_error("gpu", "texture_view_create: source texture is not a live handle");
        return res;
    }

    MTLPixelFormat fmt = opt.format != MEL_GPU_FORMAT_UNDEFINED ? mel_gpu__mtl_format(opt.format) : tex.format;
    u32            base_mip = opt.range.base_mip;
    u32            mip_count = opt.range.mip_count ? opt.range.mip_count : tex.mip_levels - base_mip;
    u32            base_layer = opt.range.base_layer;
    u32            layer_count = opt.range.layer_count ? opt.range.layer_count : tex.array_layers - base_layer;

    id<MTLTexture> srctex = (__bridge id<MTLTexture>)tex.texture;
    id<MTLTexture> view = srctex;
    bool           full = base_mip == 0 && mip_count == tex.mip_levels && base_layer == 0 && layer_count == tex.array_layers && fmt == tex.format;
    if (!full)
    {
        view = [srctex newTextureViewWithPixelFormat:fmt textureType:srctex.textureType levels:NSMakeRange(base_mip, mip_count) slices:NSMakeRange(base_layer, layer_count)];
        if (!view)
        {
            res.status = MEL_GPU_TEXTURE_VIEW_CREATE_BACKEND_FAILED;
            mel_log_error("gpu", "texture_view_create: newTextureView returned nil");
            return res;
        }
    }

    Mel_Gpu_Texture_View_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .name = opt.name },
        .view = (__bridge_retained void*)view,
        .texture = opt.texture.slot,
        .format = fmt,
        .aspect = tex.aspect,
        .base_mip = base_mip,
        .mip_count = mip_count,
        .base_layer = base_layer,
        .layer_count = layer_count,
        .usage = tex.usage,
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->texture_views, &o);
    res.value = (Mel_Gpu_Texture_View){ h };

    if (dev->bindless.enabled)
    {
        bool sampled = (tex.usage & MEL_GPU_TEXTURE_SAMPLED) != 0;
        bool storage = (tex.usage & MEL_GPU_TEXTURE_STORAGE) != 0;
        bool fits = true;
        if (sampled)
            fits = mel_gpu__bindless_slot_fits(dev, MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE, h.index) && fits;
        if (storage)
            fits = mel_gpu__bindless_slot_fits(dev, MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE, h.index) && fits;
        if (!fits)
        {
            mel_log_error("gpu", "texture_view_create '%s': bindless slot %u exceeds a heap class cap (BindlessSlotExhausted)", opt.name ? opt.name : "(unnamed)", h.index);
            mel_gpu__table_remove(dev, &dev->texture_views, h);
            id discard = (__bridge_transfer id)o.view;
            (void)discard;
            res.value = (Mel_Gpu_Texture_View){ mel_gpu_handle_null() };
            res.status = MEL_GPU_TEXTURE_VIEW_CREATE_BINDLESS_SLOT_EXHAUSTED;
            return res;
        }
        if (sampled)
            mel_gpu__bindless_register_sampled_image(dev, h.index, view);
        if (storage)
            mel_gpu__bindless_register_storage_image(dev, h.index, view);
    }
    return res;
}

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_default_view(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex) { return mel_gpu_texture_view_create(dev, .texture = tex); }

void mel_gpu_texture_view_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view)
{
    Mel_Gpu_Texture_View_Obj* o = mel_gpu__table_get(dev, &dev->texture_views, view.slot);
    if (!o)
        return;
    if (o->view)
    {
        id v = (__bridge_transfer id)o->view;
        o->view = NULL;
        (void)v;
    }
    mel_gpu__table_remove(dev, &dev->texture_views, view.slot);
}

bool mel_gpu_texture_view_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view) { return mel_gpu__table_alive(dev, &dev->texture_views, view.slot); }

static MTLSamplerAddressMode mel_gpu__wrap(Mel_Gpu_Wrap w)
{
    switch (w)
    {
    case MEL_GPU_WRAP_MIRROR_REPEAT:
        return MTLSamplerAddressModeMirrorRepeat;
    case MEL_GPU_WRAP_CLAMP_EDGE:
        return MTLSamplerAddressModeClampToEdge;
    case MEL_GPU_WRAP_CLAMP_BORDER:
        return MTLSamplerAddressModeClampToBorderColor;
    case MEL_GPU_WRAP_REPEAT:
    default:
        return MTLSamplerAddressModeRepeat;
    }
}

Mel_Gpu_Sampler_Create_Result mel_gpu_sampler_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt)
{
    Mel_Gpu_Sampler_Create_Result res = { .status = MEL_GPU_SAMPLER_CREATE_OK };
    MTLSamplerDescriptor*         sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = opt.min_filter == MEL_GPU_FILTER_LINEAR ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.magFilter = opt.mag_filter == MEL_GPU_FILTER_LINEAR ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.mipFilter = opt.mip_filter == MEL_GPU_MIPMAP_LINEAR ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
    sd.sAddressMode = mel_gpu__wrap(opt.wrap_u);
    sd.tAddressMode = mel_gpu__wrap(opt.wrap_v);
    sd.rAddressMode = mel_gpu__wrap(opt.wrap_w);
    sd.maxAnisotropy = opt.max_anisotropy >= 1.0f ? (NSUInteger)opt.max_anisotropy : 1;
    sd.lodMinClamp = opt.lod_min;
    sd.lodMaxClamp = opt.lod_max > 0.0f ? opt.lod_max : FLT_MAX;
    if (dev->bindless.enabled)
        sd.supportArgumentBuffers = YES;

    id<MTLSamplerState> ms = [dev->mtl newSamplerStateWithDescriptor:sd];
    if (!ms)
    {
        res.status = MEL_GPU_SAMPLER_CREATE_BACKEND_FAILED;
        mel_log_error("gpu", "sampler_create: newSamplerState returned nil");
        return res;
    }

    Mel_Gpu_Sampler_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .name = opt.name },
        .sampler = (__bridge_retained void*)ms,
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->samplers, &o);
    res.value = (Mel_Gpu_Sampler){ h };

    if (dev->bindless.enabled)
    {
        if (!mel_gpu__bindless_slot_fits(dev, MEL_GPU_BINDLESS_BINDING_SAMPLER, h.index))
        {
            mel_log_error("gpu", "sampler_create '%s': bindless slot %u exceeds the sampler heap cap (BindlessSlotExhausted)", opt.name ? opt.name : "(unnamed)", h.index);
            mel_gpu__table_remove(dev, &dev->samplers, h);
            id discard = (__bridge_transfer id)o.sampler;
            (void)discard;
            res.value = (Mel_Gpu_Sampler){ mel_gpu_handle_null() };
            res.status = MEL_GPU_SAMPLER_CREATE_BINDLESS_SLOT_EXHAUSTED;
            return res;
        }
        mel_gpu__bindless_register_sampler(dev, h.index, ms);
    }
    return res;
}

void mel_gpu_sampler_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    Mel_Gpu_Sampler_Obj* o = mel_gpu__table_get(dev, &dev->samplers, sampler.slot);
    if (!o)
        return;
    if (o->sampler)
    {
        id s = (__bridge_transfer id)o->sampler;
        o->sampler = NULL;
        (void)s;
    }
    mel_gpu__table_remove(dev, &dev->samplers, sampler.slot);
}

bool mel_gpu_sampler_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler) { return mel_gpu__table_alive(dev, &dev->samplers, sampler.slot); }
