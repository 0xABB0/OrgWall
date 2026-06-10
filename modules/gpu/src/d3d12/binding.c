#include "d3d_backend.h"

#include <gpu/binding.h>
#include <log/log.h>

enum
{
    MEL_D3D12_SEED_RESOURCE = 1024,
    MEL_D3D12_WALL_RESOURCE = 250000,
    MEL_D3D12_CAP_SAMPLER = 2048,
};

static u32 mel_gpu__min_u32(u32 a, u32 b) { return a < b ? a : b; }
static u32 mel_gpu__max_u32(u32 a, u32 b) { return a > b ? a : b; }

static void mel_gpu__bindless_layout(Mel_Gpu_Device* dev)
{
    dev->base_sampled_image = 0;
    dev->base_storage_buffer = dev->base_sampled_image + dev->cap_sampled_image;
    dev->base_uniform_buffer = dev->base_storage_buffer + dev->cap_storage_buffer;
    dev->base_storage_image = dev->base_uniform_buffer + dev->cap_uniform_buffer;
    dev->srv_cap = dev->base_storage_image + dev->cap_storage_image;
}

static bool mel_gpu__srv_heaps_create(Mel_Gpu_Device* dev, u32 cap, ID3D12DescriptorHeap** out_visible, ID3D12DescriptorHeap** out_mirror)
{
    D3D12_DESCRIPTOR_HEAP_DESC vd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = cap, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    D3D12_DESCRIPTOR_HEAP_DESC md = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = cap, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE };
    HRESULT                    hv = ID3D12Device_CreateDescriptorHeap(dev->d3d, &vd, &IID_ID3D12DescriptorHeap, (void**)out_visible);
    HRESULT                    hm = ID3D12Device_CreateDescriptorHeap(dev->d3d, &md, &IID_ID3D12DescriptorHeap, (void**)out_mirror);
    if (FAILED(hv) || FAILED(hm) || !*out_visible || !*out_mirror)
    {
        mel_log_error("gpu", "bindless: CBV/SRV/UAV heap creation failed at %u descriptors (visible=0x%08lx mirror=0x%08lx)", cap, (unsigned long)hv, (unsigned long)hm);
        if (*out_visible)
            ID3D12DescriptorHeap_Release(*out_visible);
        if (*out_mirror)
            ID3D12DescriptorHeap_Release(*out_mirror);
        *out_visible = *out_mirror = NULL;
        return false;
    }
    return true;
}

void mel_gpu__bindless_init(Mel_Gpu_Device* dev)
{
    dev->hw_max_sampled_image = MEL_D3D12_WALL_RESOURCE;
    dev->hw_max_storage_buffer = MEL_D3D12_WALL_RESOURCE;
    dev->hw_max_uniform_buffer = MEL_D3D12_WALL_RESOURCE;
    dev->hw_max_storage_image = MEL_D3D12_WALL_RESOURCE;
    dev->cap_sampled_image = MEL_D3D12_SEED_RESOURCE;
    dev->cap_storage_buffer = MEL_D3D12_SEED_RESOURCE;
    dev->cap_uniform_buffer = MEL_D3D12_SEED_RESOURCE;
    dev->cap_storage_image = MEL_D3D12_SEED_RESOURCE;
    mel_gpu__bindless_layout(dev);
    dev->smp_cap = MEL_D3D12_CAP_SAMPLER;

    if (!mel_gpu__srv_heaps_create(dev, dev->srv_cap, &dev->srv_heap, &dev->srv_mirror))
        return;
    D3D12_DESCRIPTOR_HEAP_DESC pd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, .NumDescriptors = dev->smp_cap, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    HRESULT                    hp = ID3D12Device_CreateDescriptorHeap(dev->d3d, &pd, &IID_ID3D12DescriptorHeap, (void**)&dev->smp_heap);
    if (FAILED(hp) || !dev->smp_heap)
    {
        mel_log_error("gpu", "bindless_init: sampler heap creation failed (0x%08lx)", (unsigned long)hp);
        ID3D12DescriptorHeap_Release(dev->srv_heap);
        ID3D12DescriptorHeap_Release(dev->srv_mirror);
        dev->srv_heap = dev->srv_mirror = NULL;
        return;
    }
    dev->srv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev->d3d, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dev->smp_inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev->d3d, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    mel_mutex_init(&dev->bindless_lock, MEL_MUTEX_PLAIN);
    dev->bindless_enabled = true;

    dev->caps.memory.bindless.max_texture_view_slots = dev->hw_max_sampled_image;
    dev->caps.memory.bindless.max_storage_buffer_slots = dev->hw_max_storage_buffer;
    dev->caps.memory.bindless.max_uniform_buffer_slots = dev->hw_max_uniform_buffer;
    dev->caps.memory.bindless.max_storage_image_slots = dev->hw_max_storage_image;
    dev->caps.memory.bindless.max_sampler_slots = dev->smp_cap;
    dev->caps.memory.bindless.growable = true;
    dev->caps.memory.bindless.seed_texture_view_slots = dev->cap_sampled_image;
    dev->caps.memory.bindless.seed_storage_buffer_slots = dev->cap_storage_buffer;
    dev->caps.memory.bindless.seed_uniform_buffer_slots = dev->cap_uniform_buffer;
    dev->caps.memory.bindless.seed_storage_image_slots = dev->cap_storage_image;
    dev->caps.memory.bindless.seed_sampler_slots = dev->smp_cap;

    mel_log_info("gpu", "bindless heap: classic growable, resource seeds %u/%u/%u/%u (walls %u), %u samplers fixed", dev->cap_sampled_image, dev->cap_storage_buffer, dev->cap_uniform_buffer, dev->cap_storage_image, (u32)MEL_D3D12_WALL_RESOURCE, dev->smp_cap);
}

void mel_gpu__bindless_destroy(Mel_Gpu_Device* dev)
{
    if (dev->srv_heap)
        ID3D12DescriptorHeap_Release(dev->srv_heap);
    if (dev->srv_mirror)
        ID3D12DescriptorHeap_Release(dev->srv_mirror);
    if (dev->smp_heap)
        ID3D12DescriptorHeap_Release(dev->smp_heap);
    dev->srv_heap = dev->srv_mirror = dev->smp_heap = NULL;
    if (dev->bindless_enabled)
        mel_mutex_destroy(&dev->bindless_lock);
    dev->bindless_enabled = false;
}

static D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__heap_cpu(ID3D12DescriptorHeap* heap, u32 inc, u32 slot)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &h);
    h.ptr += (SIZE_T)slot * inc;
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__smp_cpu(Mel_Gpu_Device* dev, u32 slot)
{
    return mel_gpu__heap_cpu(dev->smp_heap, dev->smp_inc, slot);
}

static bool mel_gpu__bindless_grow(Mel_Gpu_Device* dev, u32* cap_field, u32 hw_max, u32 need_slot, const char* klass)
{
    u32 old_caps[4] = { dev->cap_sampled_image, dev->cap_storage_buffer, dev->cap_uniform_buffer, dev->cap_storage_image };
    u32 old_bases[4] = { dev->base_sampled_image, dev->base_storage_buffer, dev->base_uniform_buffer, dev->base_storage_image };

    u32 old_cap = *cap_field;
    *cap_field = mel_gpu__min_u32(mel_gpu__max_u32(old_cap * 2, need_slot + 1), hw_max);
    mel_gpu__bindless_layout(dev);

    ID3D12DescriptorHeap* fresh_visible = NULL;
    ID3D12DescriptorHeap* fresh_mirror = NULL;
    if (!mel_gpu__srv_heaps_create(dev, dev->srv_cap, &fresh_visible, &fresh_mirror))
    {
        *cap_field = old_cap;
        mel_gpu__bindless_layout(dev);
        return false;
    }

    u32 new_bases[4] = { dev->base_sampled_image, dev->base_storage_buffer, dev->base_uniform_buffer, dev->base_storage_image };
    for (u32 c = 0; c < 4; c++)
        ID3D12Device_CopyDescriptorsSimple(dev->d3d, old_caps[c],
                                           mel_gpu__heap_cpu(fresh_mirror, dev->srv_inc, new_bases[c]),
                                           mel_gpu__heap_cpu(dev->srv_mirror, dev->srv_inc, old_bases[c]),
                                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Device_CopyDescriptorsSimple(dev->d3d, dev->srv_cap,
                                       mel_gpu__heap_cpu(fresh_visible, dev->srv_inc, 0),
                                       mel_gpu__heap_cpu(fresh_mirror, dev->srv_inc, 0),
                                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12DescriptorHeap_Release(dev->srv_mirror);
    dev->srv_mirror = fresh_mirror;
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .heap = dev->srv_heap });
    dev->srv_heap = fresh_visible;

    mel_log_warn("gpu", "bindless: %s heap grew %u -> %u (shader-visible heap rebuilt at %u descriptors)", klass, old_cap, *cap_field, dev->srv_cap);
    return true;
}

static bool mel_gpu__bindless_ensure(Mel_Gpu_Device* dev, u32* cap_field, u32 hw_max, u32 slot, const char* klass)
{
    if (slot < *cap_field)
        return true;
    if (slot >= hw_max || !mel_gpu__bindless_grow(dev, cap_field, hw_max, slot, klass))
    {
        mel_log_error("gpu", "bindless: %s slot %u exceeds the device wall %u (BindlessSlotExhausted); registration refused", klass, slot, hw_max);
        return false;
    }
    return true;
}

static void mel_gpu__srv_write_both(Mel_Gpu_Device* dev, u32 heap_slot)
{
    ID3D12Device_CopyDescriptorsSimple(dev->d3d, 1,
                                       mel_gpu__heap_cpu(dev->srv_heap, dev->srv_inc, heap_slot),
                                       mel_gpu__heap_cpu(dev->srv_mirror, dev->srv_inc, heap_slot),
                                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

static D3D12_SRV_DIMENSION mel_gpu__srv_dim(Mel_Gpu_View_Dimension d)
{
    switch (d)
    {
    case MEL_GPU_VIEW_1D:
        return D3D12_SRV_DIMENSION_TEXTURE1D;
    case MEL_GPU_VIEW_1D_ARRAY:
        return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
    case MEL_GPU_VIEW_2D_ARRAY:
        return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    case MEL_GPU_VIEW_3D:
        return D3D12_SRV_DIMENSION_TEXTURE3D;
    case MEL_GPU_VIEW_CUBE:
        return D3D12_SRV_DIMENSION_TEXTURECUBE;
    case MEL_GPU_VIEW_CUBE_ARRAY:
        return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
    case MEL_GPU_VIEW_2D:
    default:
        return D3D12_SRV_DIMENSION_TEXTURE2D;
    }
}

void mel_gpu__bindless_register_texture_view(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Texture_View_Obj* v)
{
    if (!dev->bindless_enabled)
        return;
    Mel_Gpu_Texture      tex = { v->texture };
    Mel_Gpu_Texture_Obj* t = NULL;
    if (!mel_gpu__texture_get(dev, tex, &t))
        return;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {
        .Format = v->format,
        .ViewDimension = mel_gpu__srv_dim(v->dimension),
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    };
    srv.Texture2D.MostDetailedMip = v->base_mip;
    srv.Texture2D.MipLevels = v->mip_count;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;

    mel_mutex_lock(&dev->bindless_lock);
    if (!mel_gpu__bindless_ensure(dev, &dev->cap_sampled_image, dev->hw_max_sampled_image, index, "sampled-image"))
    {
        mel_mutex_unlock(&dev->bindless_lock);
        return;
    }
    u32 heap_slot = dev->base_sampled_image + index;
    ID3D12Device_CreateShaderResourceView(dev->d3d, t->resource, &srv, mel_gpu__heap_cpu(dev->srv_mirror, dev->srv_inc, heap_slot));
    mel_gpu__srv_write_both(dev, heap_slot);
    mel_mutex_unlock(&dev->bindless_lock);
}

void mel_gpu__bindless_register_buffer(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Buffer_Obj* b, Mel_Gpu_Buffer_Usage usage)
{
    if (!dev->bindless_enabled)
        return;
    mel_mutex_lock(&dev->bindless_lock);
    if (usage & MEL_GPU_BUFFER_STORAGE)
    {
        if (!mel_gpu__bindless_ensure(dev, &dev->cap_storage_buffer, dev->hw_max_storage_buffer, index, "storage-buffer"))
        {
            mel_mutex_unlock(&dev->bindless_lock);
            return;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { .Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER };
        uav.Buffer.FirstElement = 0;
        uav.Buffer.NumElements = (UINT)(b->size / 4);
        uav.Buffer.StructureByteStride = 0;
        uav.Buffer.CounterOffsetInBytes = 0;
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        u32 heap_slot = dev->base_storage_buffer + index;
        ID3D12Device_CreateUnorderedAccessView(dev->d3d, b->resource, NULL, &uav, mel_gpu__heap_cpu(dev->srv_mirror, dev->srv_inc, heap_slot));
        mel_gpu__srv_write_both(dev, heap_slot);
    }
    if (usage & MEL_GPU_BUFFER_UNIFORM)
    {
        if (!mel_gpu__bindless_ensure(dev, &dev->cap_uniform_buffer, dev->hw_max_uniform_buffer, index, "uniform-buffer"))
        {
            mel_mutex_unlock(&dev->bindless_lock);
            return;
        }
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = { .BufferLocation = b->gpu_va, .SizeInBytes = (UINT)((b->size + 255) & ~(u64)255) };
        u32                             heap_slot = dev->base_uniform_buffer + index;
        ID3D12Device_CreateConstantBufferView(dev->d3d, &cbv, mel_gpu__heap_cpu(dev->srv_mirror, dev->srv_inc, heap_slot));
        mel_gpu__srv_write_both(dev, heap_slot);
    }
    mel_mutex_unlock(&dev->bindless_lock);
}

static D3D12_UAV_DIMENSION mel_gpu__uav_dim(Mel_Gpu_View_Dimension d)
{
    switch (d)
    {
    case MEL_GPU_VIEW_1D:
        return D3D12_UAV_DIMENSION_TEXTURE1D;
    case MEL_GPU_VIEW_1D_ARRAY:
        return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
    case MEL_GPU_VIEW_2D_ARRAY:
    case MEL_GPU_VIEW_CUBE:
    case MEL_GPU_VIEW_CUBE_ARRAY:
        return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    case MEL_GPU_VIEW_3D:
        return D3D12_UAV_DIMENSION_TEXTURE3D;
    case MEL_GPU_VIEW_2D:
    default:
        return D3D12_UAV_DIMENSION_TEXTURE2D;
    }
}

void mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Texture_View_Obj* v)
{
    if (!dev->bindless_enabled)
        return;
    Mel_Gpu_Texture      tex = { v->texture };
    Mel_Gpu_Texture_Obj* t = NULL;
    if (!mel_gpu__texture_get(dev, tex, &t))
        return;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { .Format = v->format, .ViewDimension = mel_gpu__uav_dim(v->dimension) };
    uav.Texture2D.MipSlice = v->base_mip;
    uav.Texture2D.PlaneSlice = 0;

    mel_mutex_lock(&dev->bindless_lock);
    if (!mel_gpu__bindless_ensure(dev, &dev->cap_storage_image, dev->hw_max_storage_image, index, "storage-image"))
    {
        mel_mutex_unlock(&dev->bindless_lock);
        return;
    }
    u32 heap_slot = dev->base_storage_image + index;
    ID3D12Device_CreateUnorderedAccessView(dev->d3d, t->resource, NULL, &uav, mel_gpu__heap_cpu(dev->srv_mirror, dev->srv_inc, heap_slot));
    mel_gpu__srv_write_both(dev, heap_slot);
    mel_mutex_unlock(&dev->bindless_lock);
}

void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 index, const D3D12_SAMPLER_DESC* d)
{
    if (!dev->bindless_enabled)
        return;
    mel_mutex_lock(&dev->bindless_lock);
    if (index >= dev->smp_cap)
    {
        mel_mutex_unlock(&dev->bindless_lock);
        mel_log_error("gpu", "bindless: sampler slot %u exceeds the device wall %u (BindlessSlotExhausted); registration refused", index, dev->smp_cap);
        return;
    }
    ID3D12Device_CreateSampler(dev->d3d, d, mel_gpu__smp_cpu(dev, index));
    mel_mutex_unlock(&dev->bindless_lock);
}

static void mel_gpu__bindless_hold_heap(Mel_Gpu_Command_List* cmd, ID3D12DescriptorHeap* heap)
{
    for (u32 i = 0; i < cmd->held_count; i++)
        if (cmd->held_heaps[i] == heap)
            return;
    if (cmd->held_count == cmd->held_cap)
    {
        u32 cap = cmd->held_cap ? cmd->held_cap * 2 : 4;
        cmd->held_heaps = cmd->held_heaps ? mel_realloc(cmd->dev->alloc, cmd->held_heaps, sizeof(*cmd->held_heaps) * cap) : mel_alloc(cmd->dev->alloc, sizeof(*cmd->held_heaps) * cap);
        cmd->held_cap = cap;
    }
    ID3D12DescriptorHeap_AddRef(heap);
    cmd->held_heaps[cmd->held_count++] = heap;
}

void mel_gpu__bindless_cl_release(Mel_Gpu_Command_List* cmd)
{
    for (u32 i = 0; i < cmd->held_count; i++)
        ID3D12DescriptorHeap_Release(cmd->held_heaps[i]);
    cmd->held_count = 0;
}

void mel_gpu__bindless_cl_transfer(Mel_Gpu_Command_List* cmd, u64 serial)
{
    for (u32 i = 0; i < cmd->held_count; i++)
        mel_gpu__defer_free_marked(cmd->dev, (Mel_Gpu_Deferred_Free){ .heap = cmd->held_heaps[i] }, serial);
    cmd->held_count = 0;
}

void mel_gpu__bindless_cl_bind(Mel_Gpu_Command_List* cmd)
{
    Mel_Gpu_Device* dev = cmd->dev;
    Mel_Gpu_Pipeline_Obj* o = cmd->cur_pipeline;

    mel_mutex_lock(&dev->bindless_lock);
    ID3D12DescriptorHeap* heaps[2] = { dev->srv_heap, dev->smp_heap };
    ID3D12GraphicsCommandList_SetDescriptorHeaps(cmd->list, 2, heaps);
    mel_gpu__bindless_hold_heap(cmd, dev->srv_heap);
    mel_gpu__bindless_hold_heap(cmd, dev->smp_heap);

    if (o && o->bindless)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE srv0, smp0;
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(dev->srv_heap, &srv0);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(dev->smp_heap, &smp0);
        u32 bases[4] = { dev->base_sampled_image, dev->base_storage_buffer, dev->base_uniform_buffer, dev->base_storage_image };
        for (u32 c = 0; c < 4; c++)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE h = { srv0.ptr + (UINT64)bases[c] * dev->srv_inc };
            if (o->is_compute)
                ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cmd->list, o->class_table_param[c], h);
            else
                ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cmd->list, o->class_table_param[c], h);
        }
        if (o->is_compute)
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cmd->list, o->smp_table_param, smp0);
        else
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cmd->list, o->smp_table_param, smp0);
    }
    mel_mutex_unlock(&dev->bindless_lock);
}

bool mel_gpu_bindless_available(Mel_Gpu_Device* dev) { return dev && dev->bindless_enabled; }

u32 mel_gpu_texture_view_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view)
{
    mel_assert(dev->bindless_enabled);
    mel_assert(mel_gpu_texture_view_alive(dev, view));
    return view.slot.index;
}

u32 mel_gpu_buffer_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    mel_assert(dev->bindless_enabled);
    mel_assert(mel_gpu_buffer_alive(dev, buf));
    return buf.slot.index;
}

u64 mel_gpu_buffer_device_address(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = NULL;
    if (!mel_gpu__buffer_get(dev, buf, &o))
        return 0;
    if (!(o->usage & MEL_GPU_BUFFER_DEVICE_ADDRESS))
    {
        mel_log_warn("gpu", "buffer_device_address: buffer lacks MEL_GPU_BUFFER_DEVICE_ADDRESS usage");
        return 0;
    }
    return (u64)o->gpu_va;
}

void mel_gpu_cmd_bind_bindless(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd);
    Mel_Gpu_Device* dev = cmd->dev;
    if (!dev->bindless_enabled)
        return;
    mel_gpu__bindless_cl_bind(cmd);
}
