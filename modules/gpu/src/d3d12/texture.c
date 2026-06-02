#include "d3d_backend.h"

#include <log/log.h>

#include <string.h>

DXGI_FORMAT mel_gpu__dxgi_format(Mel_Gpu_Format fmt)
{
    switch (fmt)
    {
    case MEL_GPU_FORMAT_BGRA8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case MEL_GPU_FORMAT_RGBA8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case MEL_GPU_FORMAT_RGBA8_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case MEL_GPU_FORMAT_BGRA8_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case MEL_GPU_FORMAT_RG32_FLOAT:
        return DXGI_FORMAT_R32G32_FLOAT;
    case MEL_GPU_FORMAT_RGB32_FLOAT:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case MEL_GPU_FORMAT_RGBA32_FLOAT:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case MEL_GPU_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_D32_FLOAT;
    case MEL_GPU_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case MEL_GPU_FORMAT_UNDEFINED:
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

static D3D12_RESOURCE_DIMENSION mel_gpu__tex_dim(Mel_Gpu_Texture_Kind kind)
{
    switch (kind)
    {
    case MEL_GPU_TEXTURE_1D:
        return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case MEL_GPU_TEXTURE_3D:
        return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    case MEL_GPU_TEXTURE_2D:
    default:
        return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    }
}

bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj** out)
{
    Mel_Gpu_Texture_Obj* o = mel_gpu__table_get(dev, &dev->textures, tex.slot);
    if (!o)
        return false;
    *out = o;
    return true;
}

bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj** out)
{
    Mel_Gpu_Texture_View_Obj* o = mel_gpu__table_get(dev, &dev->texture_views, view.slot);
    if (!o)
        return false;
    *out = o;
    return true;
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

    u32         mips = opt.mip_levels ? opt.mip_levels : 1;
    u32         layers = opt.array_layers ? opt.array_layers : 1;
    u32         samples = opt.sample_count ? opt.sample_count : 1;
    DXGI_FORMAT fmt = mel_gpu__dxgi_format(opt.format);
    bool        is_depth = mel_gpu_format_is_depth(opt.format);

    if (opt.memory != MEL_GPU_MEMORY_DEVICE)
        mel_log_warn("gpu", "texture_create '%s': memory role %d ignored — textures are device-local in this slice", opt.name ? opt.name : "(unnamed)", (int)opt.memory);

    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (opt.usage & MEL_GPU_TEXTURE_ATTACHMENT)
        flags |= is_depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (opt.usage & MEL_GPU_TEXTURE_STORAGE)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    u32                 height = opt.extent.height ? opt.extent.height : 1;
    u32                 dor = opt.kind == MEL_GPU_TEXTURE_3D ? (opt.extent.depth ? opt.extent.depth : 1) : layers;
    D3D12_RESOURCE_DESC rd = {
        .Dimension = mel_gpu__tex_dim(opt.kind),
        .Alignment = 0,
        .Width = opt.extent.width,
        .Height = height,
        .DepthOrArraySize = (UINT16)dor,
        .MipLevels = (UINT16)mips,
        .Format = fmt,
        .SampleDesc = { .Count = samples, .Quality = 0 },
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };
    D3D12_HEAP_PROPERTIES hp = { .Type = D3D12_HEAP_TYPE_DEFAULT, .CreationNodeMask = 1, .VisibleNodeMask = 1 };

    ID3D12Resource* resource = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(dev->d3d, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void**)&resource);
    if (FAILED(hr) || !resource)
    {
        mel_log_error("gpu", "CreateCommittedResource(texture) failed: 0x%08lx", (unsigned long)hr);
        res.status = hr == E_OUTOFMEMORY ? MEL_GPU_TEXTURE_CREATE_OOM : MEL_GPU_TEXTURE_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Texture_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.capture_replay = opt.capture_replay;
    obj.header.name = opt.name;
    obj.resource = resource;
    obj.format = fmt;
    obj.kind = opt.kind;
    obj.width = (u32)opt.extent.width;
    obj.height = height;
    obj.depth = opt.kind == MEL_GPU_TEXTURE_3D ? dor : 1;
    obj.mip_levels = mips;
    obj.array_layers = layers;
    obj.sample_count = samples;
    obj.usage = opt.usage;
    obj.is_depth = is_depth;

    res.value.slot = mel_gpu__table_insert(dev, &dev->textures, &obj);
    return res;
}

void mel_gpu_texture_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex)
{
    Mel_Gpu_Texture_Obj* o = mel_gpu__table_get(dev, &dev->textures, tex.slot);
    if (!o)
        return;
    ID3D12Resource* r = o->resource;
    bool            borrowed = o->header.ownership == MEL_GPU_OWNERSHIP_BORROWED;
    mel_gpu__table_remove(dev, &dev->textures, tex.slot);
    if (!borrowed)
        mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .resource = r });
}

bool mel_gpu_texture_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex) { return mel_gpu__table_get(dev, &dev->textures, tex.slot) != NULL; }

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_view_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View_Opt opt)
{
    Mel_Gpu_Texture_View_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_TEXTURE_VIEW_CREATE_OK };

    Mel_Gpu_Texture_Obj* tex = NULL;
    if (!dev || !mel_gpu__texture_get(dev, opt.texture, &tex))
    {
        res.status = MEL_GPU_TEXTURE_VIEW_CREATE_BAD_TEXTURE;
        mel_log_error("gpu", "texture_view_create: invalid texture handle");
        return res;
    }

    Mel_Gpu_Texture_View_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.texture = opt.texture.slot;
    obj.format = opt.format == MEL_GPU_FORMAT_UNDEFINED ? tex->format : mel_gpu__dxgi_format(opt.format);
    obj.dimension = opt.dimension;
    obj.base_mip = opt.range.base_mip;
    obj.mip_count = opt.range.mip_count ? opt.range.mip_count : (tex->mip_levels - opt.range.base_mip);
    obj.base_layer = opt.range.base_layer;
    obj.layer_count = opt.range.layer_count ? opt.range.layer_count : (tex->array_layers - opt.range.base_layer);

    res.value.slot = mel_gpu__table_insert(dev, &dev->texture_views, &obj);

    if (dev->bindless_enabled && (tex->usage & MEL_GPU_TEXTURE_SAMPLED))
        mel_gpu__bindless_register_texture_view(dev, res.value.slot.index, &obj);

    return res;
}

Mel_Gpu_Texture_View_Create_Result mel_gpu_texture_default_view(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex)
{
    Mel_Gpu_Texture_Obj*   o = NULL;
    Mel_Gpu_View_Dimension dim = MEL_GPU_VIEW_2D;
    if (dev && mel_gpu__texture_get(dev, tex, &o))
    {
        if (o->kind == MEL_GPU_TEXTURE_1D)
            dim = o->array_layers > 1 ? MEL_GPU_VIEW_1D_ARRAY : MEL_GPU_VIEW_1D;
        else if (o->kind == MEL_GPU_TEXTURE_3D)
            dim = MEL_GPU_VIEW_3D;
        else
            dim = o->array_layers > 1 ? MEL_GPU_VIEW_2D_ARRAY : MEL_GPU_VIEW_2D;
    }
    return mel_gpu_texture_view_create_opt(dev, (Mel_Gpu_Texture_View_Opt){ .texture = tex, .dimension = dim });
}

void mel_gpu_texture_view_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view)
{
    if (dev->bindless_enabled)
    {
        mel_gpu__table_remove_deferred(dev, &dev->texture_views, view.slot);
        mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .reclaim_table = &dev->texture_views, .reclaim_index = view.slot.index, .has_reclaim = true });
    }
    else
    {
        mel_gpu__table_remove(dev, &dev->texture_views, view.slot);
    }
}

bool mel_gpu_texture_view_alive(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view) { return mel_gpu__table_get(dev, &dev->texture_views, view.slot) != NULL; }

void mel_gpu_texture_write(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Region region, const void* data, usize bytes)
{
    (void)bytes;
    Mel_Gpu_Texture_Obj* o = NULL;
    if (!dev || !mel_gpu__texture_get(dev, tex, &o))
    {
        mel_assert(!"texture_write: invalid texture handle");
        return;
    }

    UINT sub = region.subresource.base_mip + region.subresource.base_layer * o->mip_levels;

    D3D12_RESOURCE_DESC td;
    ID3D12Resource_GetDesc(o->resource, &td);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = { 0 };
    UINT                               num_rows = 0;
    UINT64                             row_size = 0, total = 0;
    ID3D12Device_GetCopyableFootprints(dev->d3d, &td, sub, 1, 0, &fp, &num_rows, &row_size, &total);

    D3D12_HEAP_PROPERTIES hp = { .Type = D3D12_HEAP_TYPE_UPLOAD, .CreationNodeMask = 1, .VisibleNodeMask = 1 };
    D3D12_RESOURCE_DESC   bd = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = total,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = { .Count = 1 },
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    };
    ID3D12Resource* staging = NULL;
    if (FAILED(ID3D12Device_CreateCommittedResource(dev->d3d, &hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&staging)))
        return;

    u8* map = NULL;
    if (FAILED(ID3D12Resource_Map(staging, 0, NULL, (void**)&map)))
    {
        ID3D12Resource_Release(staging);
        return;
    }
    const u8* src = data;
    for (UINT r = 0; r < num_rows; r++)
        memcpy(map + fp.Offset + (u64)r * fp.Footprint.RowPitch, src + (u64)r * row_size, (size_t)row_size);
    ID3D12Resource_Unmap(staging, 0, NULL);

    ID3D12CommandAllocator*    allocr = NULL;
    ID3D12GraphicsCommandList* list = NULL;
    if (FAILED(ID3D12Device_CreateCommandAllocator(dev->d3d, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&allocr)) || FAILED(ID3D12Device_CreateCommandList(dev->d3d, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocr, NULL, &IID_ID3D12GraphicsCommandList, (void**)&list)))
    {
        if (allocr)
            ID3D12CommandAllocator_Release(allocr);
        ID3D12Resource_Release(staging);
        return;
    }

    D3D12_RESOURCE_BARRIER to_dst = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = { .pResource = o->resource, .Subresource = sub, .StateBefore = D3D12_RESOURCE_STATE_COMMON, .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST },
    };
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &to_dst);

    D3D12_TEXTURE_COPY_LOCATION dst_loc = { .pResource = o->resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = sub };
    D3D12_TEXTURE_COPY_LOCATION src_loc = { .pResource = staging, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = fp };
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst_loc, region.offset.width, region.offset.height, region.offset.depth, &src_loc, NULL);

    D3D12_RESOURCE_BARRIER to_read = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = { .pResource = o->resource, .Subresource = sub, .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST, .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
    };
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &to_read);
    ID3D12GraphicsCommandList_Close(list);

    ID3D12CommandList* lists[1] = { (ID3D12CommandList*)list };
    u64                serial = mel_gpu__submit_serial_next(dev);
    mel_mutex_lock(&dev->submit_lock);
    ID3D12CommandQueue_ExecuteCommandLists(dev->direct_queue, 1, lists);
    ID3D12CommandQueue_Signal(dev->direct_queue, dev->timeline, serial);
    mel_mutex_unlock(&dev->submit_lock);
    mel_gpu__wait_serial(dev, serial);
    mel_gpu__submit_complete(dev, serial);

    ID3D12GraphicsCommandList_Release(list);
    ID3D12CommandAllocator_Release(allocr);
    ID3D12Resource_Release(staging);
}
