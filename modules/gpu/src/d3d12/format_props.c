#include "d3d_backend.h"

#include <gpu/format_props.h>

static u32 mel_gpu__map_format_support(D3D12_FORMAT_SUPPORT1 s1, D3D12_FORMAT_SUPPORT2 s2)
{
    u32 out = 0;
    if (s1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)
        out |= MEL_GPU_FMT_SAMPLED;
    if (s1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)
        out |= MEL_GPU_FMT_STORAGE;
    if (s2 & (D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE))
        out |= MEL_GPU_FMT_STORAGE_ATOMIC;
    if (s1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET)
        out |= MEL_GPU_FMT_COLOR_ATTACHMENT;
    if (s1 & D3D12_FORMAT_SUPPORT1_BLENDABLE)
        out |= MEL_GPU_FMT_COLOR_BLEND;
    if (s1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)
        out |= MEL_GPU_FMT_DEPTH_ATTACHMENT;
    if (s1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)
        out |= MEL_GPU_FMT_LINEAR_FILTER;
    if (s1 & D3D12_FORMAT_SUPPORT1_BUFFER)
    {
        out |= MEL_GPU_FMT_TRANSFER_SRC;
        out |= MEL_GPU_FMT_TRANSFER_DST;
    }
    return out;
}

Mel_Gpu_Format_Properties mel_gpu_format_properties(Mel_Gpu_Device* dev, Mel_Gpu_Format format, Mel_Gpu_Tiling tiling)
{
    Mel_Gpu_Format_Properties out = { 0 };
    if (!dev)
        return out;

    DXGI_FORMAT dxgi = mel_gpu__dxgi_format(format);

    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = { .Format = dxgi };
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev->d3d, D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof fs)))
    {
        u32 features = mel_gpu__map_format_support(fs.Support1, fs.Support2);
        out.tiling_features = features;
        out.linear_tiling_features = tiling == MEL_GPU_TILING_LINEAR ? features : 0;
        if (fs.Support1 & D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER)
            out.buffer_features = MEL_GPU_FMT_VERTEX_BUFFER;
    }

    u32 sample_counts = 1;
    for (u32 s = 2; s <= 16; s *= 2)
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms = { .Format = dxgi, .SampleCount = s };
        if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(dev->d3d, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms, sizeof ms)) && ms.NumQualityLevels > 0)
            sample_counts |= s;
    }
    out.sample_counts = sample_counts;

    return out;
}
