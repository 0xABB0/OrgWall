#include "d3d_backend.h"

#include <gpu/binding.h>
#include <log/log.h>

// U14 bindless (gpu-rhi.md §6.7) — the D3D12 **floor**: Tier-3 unbounded descriptor tables. The SM 6.6
// dynamic-resource ceiling (ResourceDescriptorHeap / SamplerDescriptorHeap + HEAP_DIRECTLY_INDEXED root
// flags) needs the Agility SDK or a Win11 runtime; the in-box Windows 10 runtime rejects those flags
// ("Unsupported bit-flag set") and SM 6.6 bytecode, so the floor is the classical model (§6.7 "D3D12 floor:
// classical descriptor heaps with root-signature tables"). The shader declares per-class unbounded arrays
// (Texture2D g[] : register(t0,space0); RWByteAddressBuffer b[] : register(u0,space0); ...) and the root
// signature binds one CBV/SRV/UAV descriptor table (SRV+UAV+CBV ranges, each offset to its class base) plus
// a sampler table. The per-draw root record carries descriptor indices (the §6.7 D3D12 payload), never raw
// GPU addresses — the concrete contrast with the Vulkan-BDA mixed payload.
//
// One CBV/SRV/UAV heap holds four classes (SRV textures, UAV storage buffers, CBV uniform buffers, UAV
// storage images) at fixed base offsets; samplers live in a second heap. The descriptor is written at
// base[class] + handle.index, but the table range's OffsetInDescriptorsFromTableStart equals base[class], so
// the **shader index is handle.index** (0-based within its class) — slot == handle.index holds (§3.1).

enum
{
    MEL_D3D12_CAP_SAMPLED_IMAGE = 16384,
    MEL_D3D12_CAP_STORAGE_BUFFER = 16384,
    MEL_D3D12_CAP_UNIFORM_BUFFER = 16384,
    MEL_D3D12_CAP_STORAGE_IMAGE = 16384,
    MEL_D3D12_CAP_SAMPLER = 2048, // the shader-visible sampler-heap hard limit
};

void mel_gpu__bindless_init(Mel_Gpu_Device* dev)
{
    dev->cap_sampled_image = MEL_D3D12_CAP_SAMPLED_IMAGE;
    dev->cap_storage_buffer = MEL_D3D12_CAP_STORAGE_BUFFER;
    dev->cap_uniform_buffer = MEL_D3D12_CAP_UNIFORM_BUFFER;
    dev->cap_storage_image = MEL_D3D12_CAP_STORAGE_IMAGE;
    dev->base_sampled_image = 0;
    dev->base_storage_buffer = dev->base_sampled_image + dev->cap_sampled_image;
    dev->base_uniform_buffer = dev->base_storage_buffer + dev->cap_storage_buffer;
    dev->base_storage_image = dev->base_uniform_buffer + dev->cap_uniform_buffer;
    dev->srv_cap = dev->base_storage_image + dev->cap_storage_image;
    dev->smp_cap = MEL_D3D12_CAP_SAMPLER;

    D3D12_DESCRIPTOR_HEAP_DESC sd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = dev->srv_cap, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    D3D12_DESCRIPTOR_HEAP_DESC pd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, .NumDescriptors = dev->smp_cap, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    HRESULT                    hs = ID3D12Device_CreateDescriptorHeap(dev->d3d, &sd, &IID_ID3D12DescriptorHeap, (void**)&dev->srv_heap);
    HRESULT                    hp = ID3D12Device_CreateDescriptorHeap(dev->d3d, &pd, &IID_ID3D12DescriptorHeap, (void**)&dev->smp_heap);
    if (FAILED(hs) || FAILED(hp) || !dev->srv_heap || !dev->smp_heap)
    {
        mel_log_error("gpu", "bindless_init: shader-visible heap creation failed (srv=0x%08lx smp=0x%08lx)", (unsigned long)hs, (unsigned long)hp);
        if (dev->srv_heap)
            ID3D12DescriptorHeap_Release(dev->srv_heap);
        if (dev->smp_heap)
            ID3D12DescriptorHeap_Release(dev->smp_heap);
        dev->srv_heap = dev->smp_heap = NULL;
        return;
    }
    dev->srv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev->d3d, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dev->smp_inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev->d3d, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    dev->bindless_enabled = true;

    // Refine the caps probe's tier-3 placeholder counts to the realized heap capacities (§6.7).
    dev->caps.memory.bindless.max_texture_view_slots = dev->cap_sampled_image;
    dev->caps.memory.bindless.max_storage_buffer_slots = dev->cap_storage_buffer;
    dev->caps.memory.bindless.max_uniform_buffer_slots = dev->cap_uniform_buffer;
    dev->caps.memory.bindless.max_storage_image_slots = dev->cap_storage_image;
    dev->caps.memory.bindless.max_sampler_slots = dev->smp_cap;
}

void mel_gpu__bindless_destroy(Mel_Gpu_Device* dev)
{
    if (dev->srv_heap)
        ID3D12DescriptorHeap_Release(dev->srv_heap);
    if (dev->smp_heap)
        ID3D12DescriptorHeap_Release(dev->smp_heap);
    dev->srv_heap = dev->smp_heap = NULL;
    dev->bindless_enabled = false;
}

static D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__srv_cpu(Mel_Gpu_Device* dev, u32 slot)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dev->srv_heap, &h);
    h.ptr += (SIZE_T)slot * dev->srv_inc;
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__smp_cpu(Mel_Gpu_Device* dev, u32 slot)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dev->smp_heap, &h);
    h.ptr += (SIZE_T)slot * dev->smp_inc;
    return h;
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
    Mel_Gpu_Texture     tex = { v->texture };
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

    ID3D12Device_CreateShaderResourceView(dev->d3d, t->resource, &srv, mel_gpu__srv_cpu(dev, dev->base_sampled_image + index));
}

void mel_gpu__bindless_register_buffer(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Buffer_Obj* b, Mel_Gpu_Buffer_Usage usage)
{
    if (!dev->bindless_enabled)
        return;
    if (usage & MEL_GPU_BUFFER_STORAGE)
    {
        // Raw byte-address UAV (RWByteAddressBuffer in the shader). RAW requires R32_TYPELESS.
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { .Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER };
        uav.Buffer.FirstElement = 0;
        uav.Buffer.NumElements = (UINT)(b->size / 4);
        uav.Buffer.StructureByteStride = 0;
        uav.Buffer.CounterOffsetInBytes = 0;
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        ID3D12Device_CreateUnorderedAccessView(dev->d3d, b->resource, NULL, &uav, mel_gpu__srv_cpu(dev, dev->base_storage_buffer + index));
    }
    if (usage & MEL_GPU_BUFFER_UNIFORM)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = { .BufferLocation = b->gpu_va, .SizeInBytes = (UINT)((b->size + 255) & ~(u64)255) };
        ID3D12Device_CreateConstantBufferView(dev->d3d, &cbv, mel_gpu__srv_cpu(dev, dev->base_uniform_buffer + index));
    }
}

void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 index, const D3D12_SAMPLER_DESC* d)
{
    if (!dev->bindless_enabled)
        return;
    ID3D12Device_CreateSampler(dev->d3d, d, mel_gpu__smp_cpu(dev, index));
}

// ---- public surface ----

bool mel_gpu_bindless_available(Mel_Gpu_Device* dev) { return dev && dev->bindless_enabled; }

// The shader index is the 0-based per-class handle index: the descriptor lives at heap slot
// base[class] + index, but the table range's OffsetInDescriptorsFromTableStart already adds base[class], so
// the shader array g_class[index] resolves to that heap slot. slot == handle.index (§3.1).
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
    // D3D12 buffers always have a GPU VA, but the root-record payload is descriptor_indices, not pointers
    // (§6.7) — the shader cannot dereference this as a buffer-reference the way Vulkan-BDA does. The VA is
    // real (root CBV / VBV / IBV consume it); gate on the explicit DEVICE_ADDRESS usage for API symmetry.
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
    ID3D12DescriptorHeap* heaps[2] = { dev->srv_heap, dev->smp_heap };
    ID3D12GraphicsCommandList_SetDescriptorHeaps(cmd->list, 2, heaps);
}
