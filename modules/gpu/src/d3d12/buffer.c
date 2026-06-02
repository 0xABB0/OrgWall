#include "d3d_backend.h"

#include <log/log.h>

#include <string.h>

static D3D12_HEAP_TYPE mel_gpu__heap_type(Mel_Gpu_Memory_Role role)
{
    switch (role)
    {
    case MEL_GPU_MEMORY_UPLOAD:
        return D3D12_HEAP_TYPE_UPLOAD;
    case MEL_GPU_MEMORY_READBACK:
        return D3D12_HEAP_TYPE_READBACK;
    default:
        return D3D12_HEAP_TYPE_DEFAULT;
    }
}

static D3D12_RESOURCE_DESC mel_gpu__buffer_desc(u64 size, bool storage)
{
    return (D3D12_RESOURCE_DESC){
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = { .Count = 1, .Quality = 0 },
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = storage ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE,
    };
}

// DEVICE-with-data: stage into an UPLOAD committed resource, then immediate copy on a transient DIRECT list
// (mirrors the Vulkan staging path). Buffers ride common-state promotion, so the copy needs no barriers.
static bool mel_gpu__upload_via_copy(Mel_Gpu_Device* dev, ID3D12Resource* dst, const void* data, u64 size)
{
    D3D12_HEAP_PROPERTIES hp = { .Type = D3D12_HEAP_TYPE_UPLOAD, .CreationNodeMask = 1, .VisibleNodeMask = 1 };
    D3D12_RESOURCE_DESC   rd = mel_gpu__buffer_desc(size, false);
    ID3D12Resource*       staging = NULL;
    if (FAILED(ID3D12Device_CreateCommittedResource(dev->d3d, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&staging)))
        return false;

    void* m = NULL;
    if (FAILED(ID3D12Resource_Map(staging, 0, NULL, &m)))
    {
        ID3D12Resource_Release(staging);
        return false;
    }
    memcpy(m, data, (size_t)size);
    ID3D12Resource_Unmap(staging, 0, NULL);

    ID3D12CommandAllocator*    allocr = NULL;
    ID3D12GraphicsCommandList* list = NULL;
    if (FAILED(ID3D12Device_CreateCommandAllocator(dev->d3d, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&allocr)) || FAILED(ID3D12Device_CreateCommandList(dev->d3d, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocr, NULL, &IID_ID3D12GraphicsCommandList, (void**)&list)))
    {
        if (allocr)
            ID3D12CommandAllocator_Release(allocr);
        ID3D12Resource_Release(staging);
        return false;
    }

    ID3D12GraphicsCommandList_CopyBufferRegion(list, dst, 0, staging, 0, size);
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
    return true;
}

Mel_Gpu_Buffer_Create_Result mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    Mel_Gpu_Buffer_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_BUFFER_CREATE_OK };

    if (!dev || opt.size == 0)
    {
        res.status = MEL_GPU_BUFFER_CREATE_BAD_PARAMS;
        return res;
    }

    bool                  device_local = opt.memory == MEL_GPU_MEMORY_DEVICE;
    bool                  storage = (opt.usage & MEL_GPU_BUFFER_STORAGE) != 0;
    D3D12_HEAP_PROPERTIES hp = { .Type = mel_gpu__heap_type(opt.memory), .CreationNodeMask = 1, .VisibleNodeMask = 1 };
    D3D12_RESOURCE_DESC   rd = mel_gpu__buffer_desc(opt.size, storage);
    D3D12_RESOURCE_STATES init = hp.Type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : (hp.Type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON);

    ID3D12Resource* resource = NULL;
    HRESULT         hr = ID3D12Device_CreateCommittedResource(dev->d3d, &hp, D3D12_HEAP_FLAG_NONE, &rd, init, NULL, &IID_ID3D12Resource, (void**)&resource);
    if (FAILED(hr) || !resource)
    {
        mel_log_error("gpu", "CreateCommittedResource(buffer %llu) failed: 0x%08lx", (unsigned long long)opt.size, (unsigned long)hr);
        res.status = hr == E_OUTOFMEMORY ? MEL_GPU_BUFFER_CREATE_OOM : MEL_GPU_BUFFER_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Buffer_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.capture_replay = opt.capture_replay;
    obj.header.name = opt.name;
    obj.resource = resource;
    obj.size = opt.size;
    obj.host_visible = !device_local;
    obj.usage = opt.usage;
    obj.gpu_va = ID3D12Resource_GetGPUVirtualAddress(resource);

    if (obj.host_visible)
    {
        if (FAILED(ID3D12Resource_Map(resource, 0, NULL, &obj.mapped)))
            obj.mapped = NULL;
    }

    if (opt.data)
    {
        if (device_local)
        {
            if (!mel_gpu__upload_via_copy(dev, resource, opt.data, opt.size))
            {
                ID3D12Resource_Release(resource);
                res.status = MEL_GPU_BUFFER_CREATE_VK_FAILED;
                return res;
            }
        }
        else if (obj.mapped)
        {
            memcpy(obj.mapped, opt.data, (size_t)opt.size);
        }
    }

    res.value.slot = mel_gpu__table_insert(dev, &dev->buffers, &obj);

    // U14: register the buffer's heap descriptor at its reserved slot (STORAGE -> UAV, UNIFORM -> CBV) so it
    // is reachable by descriptor index for the lifetime of the handle (gpu-rhi.md §6.7).
    if (dev->bindless_enabled && (opt.usage & (MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_UNIFORM)))
        mel_gpu__bindless_register_buffer(dev, res.value.slot.index, &obj, opt.usage);

    return res;
}

void mel_gpu_buffer_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o)
        return;
    bool            borrowed = o->header.ownership == MEL_GPU_OWNERSHIP_BORROWED;
    ID3D12Resource* r = o->resource;
    if (borrowed)
    {
        // Imported (Borrowed): no underlying release; the slot reuses immediately (gpu-rhi.md §3.1).
        mel_gpu__table_remove(dev, &dev->buffers, buf.slot);
        return;
    }
    // U3 future-gated retirement: the generation rolls now (use-after-free stays loud), the COM object and
    // the slot index are reclaimed only once in-flight submissions retire (gpu-rhi.md §3.3).
    mel_gpu__table_remove_deferred(dev, &dev->buffers, buf.slot);
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .resource = r, .reclaim_table = &dev->buffers, .reclaim_index = buf.slot.index, .has_reclaim = true });
}

bool mel_gpu_buffer_alive(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf) { return mel_gpu__table_get(dev, &dev->buffers, buf.slot) != NULL; }

void mel_gpu_buffer_write(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, const void* data, usize bytes)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    mel_assert(o != NULL);
    if (!o)
        return;
    if (o->host_visible && o->mapped)
        memcpy(o->mapped, data, bytes);
    else
        mel_gpu__upload_via_copy(dev, o->resource, data, bytes);
}

void* mel_gpu_buffer_mapped(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    return o ? o->mapped : NULL;
}

u32 mel_gpu_buffer_make_resident(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    (void)buf;
    if (dev->caps.memory.residency_control < MEL_GPU_RESIDENCY_EXPLICIT)
    {
        mel_log_warn("gpu", "make_resident: explicit residency unavailable on this device; no-op");
        return MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_WARNED);
    }
    return MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK);
}

u32 mel_gpu_buffer_evict(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    (void)buf;
    if (dev->caps.memory.residency_control < MEL_GPU_RESIDENCY_EXPLICIT)
    {
        mel_log_warn("gpu", "evict: explicit residency unavailable on this device; no-op");
        return MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_WARNED);
    }
    return MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK);
}

bool mel_gpu__buffer_resource(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, ID3D12Resource** out)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o)
        return false;
    *out = o->resource;
    return true;
}

bool mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, Mel_Gpu_Buffer_Obj** out)
{
    Mel_Gpu_Buffer_Obj* o = mel_gpu__table_get(dev, &dev->buffers, buf.slot);
    if (!o)
        return false;
    *out = o;
    return true;
}

Mel_Gpu_Buffer mel_gpu_buffer_import(Mel_Gpu_Device* dev, void* native_buffer, usize size, const char* name)
{
    Mel_Gpu_Buffer_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_BORROWED;
    obj.header.name = name;
    obj.resource = (ID3D12Resource*)native_buffer;
    obj.size = size;
    obj.host_visible = false;
    if (obj.resource)
        obj.gpu_va = ID3D12Resource_GetGPUVirtualAddress(obj.resource);
    Mel_Gpu_Buffer h = { mel_gpu__table_insert(dev, &dev->buffers, &obj) };
    return h;
}
