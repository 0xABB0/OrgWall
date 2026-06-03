#include "wgpu_backend.h"

#include <log/log.h>

#include <float.h>
#include <string.h>

static WGPUBufferUsage mel_gpu__buffer_usage(Mel_Gpu_Buffer_Usage u, Mel_Gpu_Memory_Role mem)
{
    WGPUBufferUsage out = 0;
    if (u & MEL_GPU_BUFFER_VERTEX)
        out |= WGPUBufferUsage_Vertex;
    if (u & MEL_GPU_BUFFER_INDEX)
        out |= WGPUBufferUsage_Index;
    if (u & MEL_GPU_BUFFER_UNIFORM)
        out |= WGPUBufferUsage_Uniform;
    if (u & MEL_GPU_BUFFER_STORAGE)
        out |= WGPUBufferUsage_Storage;
    if (u & MEL_GPU_BUFFER_INDIRECT)
        out |= WGPUBufferUsage_Indirect;
    if (u & MEL_GPU_BUFFER_TRANSFER_SRC)
        out |= WGPUBufferUsage_CopySrc;
    if (u & MEL_GPU_BUFFER_TRANSFER_DST)
        out |= WGPUBufferUsage_CopyDst;
    if (mem == MEL_GPU_MEMORY_READBACK)
        out |= WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    if (mem == MEL_GPU_MEMORY_UPLOAD)
        out |= WGPUBufferUsage_CopyDst;
    return out;
}

Mel_Gpu_Buffer_Create_Result mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    Mel_Gpu_Buffer_Create_Result res = { .status = MEL_GPU_BUFFER_CREATE_OK };
    if (!dev || opt.size == 0)
    {
        res.status = MEL_GPU_BUFFER_CREATE_BAD_PARAMS;
        mel_log_error("gpu", "buffer_create: null device or zero size");
        return res;
    }

    bool readback = opt.memory == MEL_GPU_MEMORY_READBACK;
    bool upload = opt.memory == MEL_GPU_MEMORY_UPLOAD;
    bool initial_at_creation = opt.data && !readback;

    WGPUBufferDescriptor desc = {
        .label = mel_gpu__sv(opt.name),
        .usage = mel_gpu__buffer_usage(opt.usage, opt.memory),
        .size = opt.size,
        .mappedAtCreation = initial_at_creation,
    };

    WGPUBuffer wb = wgpuDeviceCreateBuffer(dev->wgpu, &desc);
    if (!wb)
    {
        res.status = MEL_GPU_BUFFER_CREATE_OOM;
        mel_log_error("gpu", "buffer_create: wgpuDeviceCreateBuffer returned null for %zu bytes", opt.size);
        return res;
    }

    if (initial_at_creation)
    {
        void* ptr = wgpuBufferGetMappedRange(wb, 0, opt.size);
        if (ptr)
            memcpy(ptr, opt.data, opt.size);
        wgpuBufferUnmap(wb);
    }
    else if (opt.data)
    {
        mel_log_warn("gpu", "buffer_create: initial data ignored for readback buffer '%s'", opt.name ? opt.name : "(unnamed)");
    }

    Mel_Gpu_Buffer_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .capture_replay = opt.capture_replay, .name = opt.name },
        .wgpu = wb,
        .size = opt.size,
        .shadow = NULL,
        .host_visible = upload || readback,
        .readback = readback,
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->buffers, &o);
    res.value = (Mel_Gpu_Buffer){ h };
    return res;
}

void mel_gpu_buffer_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o)
        return;
    if (o->shadow)
    {
        mel_dealloc(dev->alloc, o->shadow);
        o->shadow = NULL;
    }
    if (o->wgpu)
    {
        wgpuBufferRelease(o->wgpu);
        o->wgpu = NULL;
    }
    mel_gpu__table_remove(dev, &dev->buffers, buf.slot);
}

bool mel_gpu_buffer_alive(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf) { return mel_gpu__table_alive(dev, &dev->buffers, buf.slot); }

void mel_gpu_buffer_write(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, const void* data, usize size)
{
    Mel_Gpu_Buffer_Obj o;
    if (!mel_gpu__buffer_get(dev, buf, &o))
    {
        mel_log_error("gpu", "buffer_write: invalid buffer handle");
        return;
    }
    if (o.readback)
    {
        mel_log_error("gpu", "buffer_write: buffer '%s' is a readback buffer; writes are not supported", o.header.name ? o.header.name : "(unnamed)");
        return;
    }
    if (size > o.size)
        size = o.size;
    wgpuQueueWriteBuffer(dev->queue, o.wgpu, 0, data, size);
}

typedef struct
{
    bool done;
    bool ok;
} Mel_Gpu_Map_Request;

static void mel_gpu__map_cb(WGPUMapAsyncStatus status, WGPUStringView message, void* u1, void* u2)
{
    (void)u2;
    Mel_Gpu_Map_Request* req = (Mel_Gpu_Map_Request*)u1;
    req->done = true;
    req->ok = status == WGPUMapAsyncStatus_Success;
    if (!req->ok)
        mel_log_error("gpu", "buffer map failed (status %d): %.*s", (int)status, message.data ? (int)message.length : 0, message.data ? message.data : "");
}

void* mel_gpu_buffer_mapped(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o || !o->readback)
        return NULL;

    Mel_Gpu_Map_Request       req = { 0 };
    WGPUBufferMapCallbackInfo cbi = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = mel_gpu__map_cb, .userdata1 = &req };
    wgpuBufferMapAsync(o->wgpu, WGPUMapMode_Read, 0, o->size, cbi);

    if (!mel_gpu__drain_sync(dev, &req.done, "buffer_mapped") || !req.ok)
        return NULL;

    const void* src = wgpuBufferGetConstMappedRange(o->wgpu, 0, o->size);
    if (!src)
    {
        mel_log_error("gpu", "buffer_mapped: GetConstMappedRange returned null for '%s' after a successful map", o->header.name ? o->header.name : "(unnamed)");
        wgpuBufferUnmap(o->wgpu);
        return NULL;
    }

    if (!o->shadow)
        o->shadow = mel_alloc(dev->alloc, o->size);
    memcpy(o->shadow, src, o->size);
    wgpuBufferUnmap(o->wgpu);
    return o->shadow;
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
    (void)dev;
    (void)native_buffer;
    (void)size;
    (void)name;
    mel_log_error("gpu", "buffer_import: external-memory import is not implemented on the WebGPU backend (MissingFeature)");
    return (Mel_Gpu_Buffer){ mel_gpu_handle_null() };
}

static WGPUTextureUsage mel_gpu__texture_usage(Mel_Gpu_Texture_Usage u)
{
    WGPUTextureUsage out = 0;
    if (u & MEL_GPU_TEXTURE_SAMPLED)
        out |= WGPUTextureUsage_TextureBinding;
    if (u & MEL_GPU_TEXTURE_STORAGE)
        out |= WGPUTextureUsage_StorageBinding;
    if (u & MEL_GPU_TEXTURE_ATTACHMENT)
        out |= WGPUTextureUsage_RenderAttachment;
    if (u & MEL_GPU_TEXTURE_COPY_SRC)
        out |= WGPUTextureUsage_CopySrc;
    if (u & MEL_GPU_TEXTURE_COPY_DST)
        out |= WGPUTextureUsage_CopyDst;
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

    WGPUTextureFormat fmt = mel_gpu__wgpu_format(opt.format);
    if (fmt == WGPUTextureFormat_Undefined)
    {
        res.status = MEL_GPU_TEXTURE_CREATE_BAD_PARAMS;
        mel_log_error("gpu", "texture_create: format %d has no WebGPU mapping", (int)opt.format);
        return res;
    }

    u32 depth_or_layers = opt.kind == MEL_GPU_TEXTURE_3D ? (opt.extent.depth ? opt.extent.depth : 1) : (opt.array_layers ? opt.array_layers : 1);

    WGPUTextureDescriptor desc = {
        .label = mel_gpu__sv(opt.name),
        .usage = mel_gpu__texture_usage(opt.usage),
        .dimension = opt.kind == MEL_GPU_TEXTURE_3D ? WGPUTextureDimension_3D : WGPUTextureDimension_2D,
        .size = { opt.extent.width, opt.extent.height, depth_or_layers },
        .format = fmt,
        .mipLevelCount = opt.mip_levels ? opt.mip_levels : 1,
        .sampleCount = opt.sample_count ? opt.sample_count : 1,
    };

    WGPUTexture wt = wgpuDeviceCreateTexture(dev->wgpu, &desc);
    if (!wt)
    {
        res.status = MEL_GPU_TEXTURE_CREATE_OOM;
        mel_log_error("gpu", "texture_create: wgpuDeviceCreateTexture returned null (%ux%u)", opt.extent.width, opt.extent.height);
        return res;
    }

    Mel_Gpu_Texture_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .capture_replay = opt.capture_replay, .name = opt.name },
        .wgpu = wt,
        .format = fmt,
        .aspect = mel_gpu_format_is_depth(opt.format) ? MEL_GPU_ASPECT_DEPTH : MEL_GPU_ASPECT_COLOR,
        .width = opt.extent.width,
        .height = opt.extent.height,
        .depth = opt.kind == MEL_GPU_TEXTURE_3D ? depth_or_layers : 1,
        .mip_levels = desc.mipLevelCount,
        .array_layers = opt.kind == MEL_GPU_TEXTURE_3D ? 1 : depth_or_layers,
        .sample_count = desc.sampleCount,
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
    if (o->wgpu)
    {
        wgpuTextureRelease(o->wgpu);
        o->wgpu = NULL;
    }
    mel_gpu__table_remove(dev, &dev->textures, tex.slot);
}

bool mel_gpu_texture_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex) { return mel_gpu__table_alive(dev, &dev->textures, tex.slot); }

void mel_gpu_texture_write(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Region region, const void* data, usize bytes)
{
    Mel_Gpu_Texture_Obj o;
    if (!mel_gpu__texture_get(dev, tex, &o))
    {
        mel_log_error("gpu", "texture_write: invalid texture handle");
        return;
    }

    u32 w = region.extent.width ? region.extent.width : o.width;
    u32 h = region.extent.height ? region.extent.height : o.height;
    u32 d = region.extent.depth ? region.extent.depth : 1;
    u32 bpp = mel_gpu_format_bytes(mel_gpu__wgpu_format_to_mel(o.format));
    u32 row_pitch = region.row_pitch ? region.row_pitch : w * bpp;

    WGPUTexelCopyTextureInfo dst = {
        .texture = o.wgpu,
        .mipLevel = region.subresource.base_mip,
        .origin = { region.offset.width, region.offset.height, region.offset.depth + region.subresource.base_layer },
        .aspect = WGPUTextureAspect_All,
    };
    WGPUTexelCopyBufferLayout layout = { .offset = 0, .bytesPerRow = row_pitch, .rowsPerImage = h };
    WGPUExtent3D              size = { w, h, d };
    wgpuQueueWriteTexture(dev->queue, &dst, data, bytes, &layout, &size);
}

static WGPUTextureViewDimension mel_gpu__view_dim(Mel_Gpu_View_Dimension d)
{
    switch (d)
    {
    case MEL_GPU_VIEW_1D:
        /* The zero default; the caller did not request a dimension. Let Dawn infer
           it from the texture's dimension + layer count (a default 2D-texture view
           is 2D, not 1D). Explicit 1D views are vanishingly rare and re-expressible
           through the array/cube variants the engine actually emits. */
        return WGPUTextureViewDimension_Undefined;
    case MEL_GPU_VIEW_2D_ARRAY:
        return WGPUTextureViewDimension_2DArray;
    case MEL_GPU_VIEW_3D:
        return WGPUTextureViewDimension_3D;
    case MEL_GPU_VIEW_CUBE:
        return WGPUTextureViewDimension_Cube;
    case MEL_GPU_VIEW_CUBE_ARRAY:
        return WGPUTextureViewDimension_CubeArray;
    case MEL_GPU_VIEW_2D:
    default:
        return WGPUTextureViewDimension_2D;
    }
}

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_view_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View_Opt opt)
{
    Mel_Gpu_Texture_View_Create_Result res = { .status = MEL_GPU_TEXTURE_VIEW_CREATE_OK };
    Mel_Gpu_Texture_Obj                tex;
    if (!mel_gpu__texture_get(dev, opt.texture, &tex))
    {
        res.status = MEL_GPU_TEXTURE_VIEW_CREATE_BAD_TEXTURE;
        mel_log_error("gpu", "texture_view_create: source texture is not a live handle");
        return res;
    }

    WGPUTextureFormat fmt = opt.format != MEL_GPU_FORMAT_UNDEFINED ? mel_gpu__wgpu_format(opt.format) : tex.format;
    u32               base_mip = opt.range.base_mip;
    u32               mip_count = opt.range.mip_count ? opt.range.mip_count : tex.mip_levels - base_mip;
    u32               base_layer = opt.range.base_layer;
    u32               layer_count = opt.range.layer_count ? opt.range.layer_count : tex.array_layers - base_layer;

    WGPUTextureViewDescriptor desc = {
        .label = mel_gpu__sv(opt.name),
        .format = fmt,
        .dimension = mel_gpu__view_dim(opt.dimension),
        .baseMipLevel = base_mip,
        .mipLevelCount = mip_count,
        .baseArrayLayer = base_layer,
        .arrayLayerCount = layer_count,
        .aspect = WGPUTextureAspect_All,
    };

    WGPUTextureView view = wgpuTextureCreateView(tex.wgpu, &desc);
    if (!view)
    {
        res.status = MEL_GPU_TEXTURE_VIEW_CREATE_VK_FAILED;
        mel_log_error("gpu", "texture_view_create: wgpuTextureCreateView returned null");
        return res;
    }

    Mel_Gpu_Texture_View_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .name = opt.name },
        .wgpu = view,
        .texture = opt.texture.slot,
        .format = fmt,
        .aspect = tex.aspect,
        .base_mip = base_mip,
        .mip_count = mip_count,
        .base_layer = base_layer,
        .layer_count = layer_count,
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->texture_views, &o);
    res.value = (Mel_Gpu_Texture_View){ h };
    return res;
}

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_default_view(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex)
{
    return mel_gpu_texture_view_create(dev, .texture = tex);
}

void mel_gpu_texture_view_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view)
{
    Mel_Gpu_Texture_View_Obj* o = mel_gpu__table_get(dev, &dev->texture_views, view.slot);
    if (!o)
        return;
    if (o->wgpu)
    {
        wgpuTextureViewRelease(o->wgpu);
        o->wgpu = NULL;
    }
    mel_gpu__table_remove(dev, &dev->texture_views, view.slot);
}

bool mel_gpu_texture_view_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view) { return mel_gpu__table_alive(dev, &dev->texture_views, view.slot); }

static WGPUAddressMode mel_gpu__wrap(Mel_Gpu_Wrap w)
{
    switch (w)
    {
    case MEL_GPU_WRAP_MIRROR_REPEAT:
        return WGPUAddressMode_MirrorRepeat;
    case MEL_GPU_WRAP_CLAMP_EDGE:
    case MEL_GPU_WRAP_CLAMP_BORDER:
        return WGPUAddressMode_ClampToEdge;
    case MEL_GPU_WRAP_REPEAT:
    default:
        return WGPUAddressMode_Repeat;
    }
}

Mel_Gpu_Sampler_Create_Result mel_gpu_sampler_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Sampler_Opt opt)
{
    Mel_Gpu_Sampler_Create_Result res = { .status = MEL_GPU_SAMPLER_CREATE_OK };

    WGPUSamplerDescriptor sd = {
        .label = mel_gpu__sv(opt.name),
        .addressModeU = mel_gpu__wrap(opt.wrap_u),
        .addressModeV = mel_gpu__wrap(opt.wrap_v),
        .addressModeW = mel_gpu__wrap(opt.wrap_w),
        .magFilter = opt.mag_filter == MEL_GPU_FILTER_LINEAR ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest,
        .minFilter = opt.min_filter == MEL_GPU_FILTER_LINEAR ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest,
        .mipmapFilter = opt.mip_filter == MEL_GPU_MIPMAP_LINEAR ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest,
        .lodMinClamp = opt.lod_min,
        .lodMaxClamp = opt.lod_max > 0.0f ? opt.lod_max : 32.0f,
        .maxAnisotropy = opt.max_anisotropy >= 1.0f ? (u16)opt.max_anisotropy : 1,
    };

    WGPUSampler ws = wgpuDeviceCreateSampler(dev->wgpu, &sd);
    if (!ws)
    {
        res.status = MEL_GPU_SAMPLER_CREATE_VK_FAILED;
        mel_log_error("gpu", "sampler_create: wgpuDeviceCreateSampler returned null");
        return res;
    }

    Mel_Gpu_Sampler_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .name = opt.name },
        .wgpu = ws,
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->samplers, &o);
    res.value = (Mel_Gpu_Sampler){ h };
    return res;
}

void mel_gpu_sampler_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler)
{
    Mel_Gpu_Sampler_Obj* o = mel_gpu__table_get(dev, &dev->samplers, sampler.slot);
    if (!o)
        return;
    if (o->wgpu)
    {
        wgpuSamplerRelease(o->wgpu);
        o->wgpu = NULL;
    }
    mel_gpu__table_remove(dev, &dev->samplers, sampler.slot);
}

bool mel_gpu_sampler_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler) { return mel_gpu__table_alive(dev, &dev->samplers, sampler.slot); }
