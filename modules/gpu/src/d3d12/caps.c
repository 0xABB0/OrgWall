#include "d3d_backend.h"

#include <string.h>

// UTF-16 DXGI adapter description → UTF-8 caps.adapter.name.
static void mel_gpu__wide_to_utf8(const WCHAR* w, char* out, int out_cap)
{
    if (out_cap <= 0)
        return;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, out_cap, NULL, NULL);
    if (n <= 0)
        out[0] = 0;
}

void mel_gpu__caps_from_adapter(IDXGIAdapter1* adapter, Mel_Gpu_Caps* out)
{
    *out = (Mel_Gpu_Caps){ 0 };

    DXGI_ADAPTER_DESC1 d;
    if (FAILED(IDXGIAdapter1_GetDesc1(adapter, &d)))
        return;

    mel_gpu__wide_to_utf8(d.Description, out->adapter.name, (int)sizeof out->adapter.name);
    out->adapter.vendor_id = d.VendorId;
    out->adapter.device_id = d.DeviceId;
    out->adapter.driver_version = 0; // DXGI exposes the UMD version only via CheckInterfaceSupport; deferred.
    out->adapter.luid = ((u64)(u32)d.AdapterLuid.HighPart << 32) | (u64)(u32)d.AdapterLuid.LowPart;
    out->adapter.has_luid = true; // the LUID is the D3D12 adapter identity (no Vulkan-style UUID).

    bool software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    out->adapter.adapter_type = software ? MEL_GPU_ADAPTER_SOFTWARE
                                         : (d.DedicatedVideoMemory > 0 ? MEL_GPU_ADAPTER_DISCRETE : MEL_GPU_ADAPTER_INTEGRATED);

    out->memory.device_local_bytes = d.DedicatedVideoMemory;
    out->memory.host_visible_bytes = d.SharedSystemMemory;
    out->memory.persistent_map = true;
    out->memory.residency_control = MEL_GPU_RESIDENCY_BUDGET_ONLY; // IDXGIAdapter3::QueryVideoMemoryInfo (U8).

    // ID3D12Fence carries a 64-bit monotonic value — a native timeline (U17). Confirmed at device-create.
    out->queues.timeline = MEL_GPU_TIMELINE_NATIVE;

    // Anisotropic filtering up to 16x is a feature-level guarantee on every D3D12 device (FL >= 9.3), so it
    // is device-independent and reported here; the U11 sampler clamp honors it.
    out->sampler.anisotropy = true;
    out->sampler.max_anisotropy = 16.0f;
}

void mel_gpu__caps_refine_device(ID3D12Device* dev, ID3D12CommandQueue* queue, Mel_Gpu_Caps* out)
{
    // U14 bindless tier (gpu-rhi.md §6.7). ResourceBindingTier 3 is the dynamic-resource ceiling
    // (ResourceDescriptorHeap / SamplerDescriptorHeap, SM 6.6); below it the per-class heap caps differ and
    // the realized capacity is decided in U14, so Phase 0 reports `full` only at Tier 3, else `none`.
    D3D12_FEATURE_DATA_D3D12_OPTIONS o = { 0 };
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof o)))
        out->memory.bindless.tier = o.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 ? MEL_GPU_TIER_FULL : MEL_GPU_TIER_NONE;
    if (out->memory.bindless.tier == MEL_GPU_TIER_FULL)
    {
        // Tier 3 floor: ~1,000,000 CBV/SRV/UAV per heap, 2048 samplers. U14 pins the realized heap sizes.
        out->memory.bindless.max_texture_view_slots = 1000000;
        out->memory.bindless.max_storage_buffer_slots = 1000000;
        out->memory.bindless.max_uniform_buffer_slots = 1000000;
        out->memory.bindless.max_storage_image_slots = 1000000;
        out->memory.bindless.max_sampler_slots = 2048;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1 = { 0 };
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof o1)))
    {
        out->shader.wave_ops = o1.WaveOps;
        out->shader.int64 = o1.Int64ShaderOps;
        out->shader.subgroup_size_min = o1.WaveLaneCountMin;
        out->shader.subgroup_size_max = o1.WaveLaneCountMax;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4 = { 0 };
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS4, &o4, sizeof o4)))
    {
        out->shader.int16 = o4.Native16BitShaderOpsSupported;
        out->shader.fp16 = o4.Native16BitShaderOpsSupported;
    }

    // UMA / ReBAR split (gpu-rhi.md §3.4 host_visible_device_local). A cache-coherent UMA part is full_uma;
    // a non-coherent UMA part is the rebar-class host-visible window. Discrete ReBAR detection via the GPU
    // upload heap (OPTIONS16) needs the Agility SDK and lands with U8.
    D3D12_FEATURE_DATA_ARCHITECTURE1 arch = { .NodeIndex = 0 };
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof arch)) && arch.UMA)
        out->memory.host_visible_device_local = arch.CacheCoherentUMA ? MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_FULL_UMA : MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_REBAR;

    // Timestamp period from the direct queue's tick frequency (U24 timestamps build on this).
    if (queue)
    {
        UINT64 freq = 0;
        if (SUCCEEDED(ID3D12CommandQueue_GetTimestampFrequency(queue, &freq)) && freq)
        {
            out->queries.timestamp = MEL_GPU_TIMESTAMP_NATIVE;
            out->queries.timestamp_period_ns = 1.0e9 / (f64)freq;
            out->queries.timestamp_compute_and_graphics = true;
        }
    }
}
