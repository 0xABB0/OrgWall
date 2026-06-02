#include "d3d_backend.h"

// U8 residency (gpu-rhi.md §3.4). D3D12 exposes a real budget + current usage via QueryVideoMemoryInfo, so
// residency_control is reported budget_only; explicit make_resident/evict (MakeResident / Evict) is a later
// additive tier. The local memory segment is the device-local budget.
Mel_Gpu_Memory_Budget mel_gpu_memory_budget(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Memory_Budget b = { 0 };
    if (!dev)
        return b;

    IDXGIAdapter3* a3 = NULL;
    if (SUCCEEDED(IDXGIAdapter1_QueryInterface(dev->adapter->dxgi, &IID_IDXGIAdapter3, (void**)&a3)) && a3)
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO info = { 0 };
        if (SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(a3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        {
            b.budget_bytes = info.Budget;
            b.usage_bytes = info.CurrentUsage;
        }
        IDXGIAdapter3_Release(a3);
    }

    if (b.budget_bytes == 0)
        b.budget_bytes = dev->caps.memory.device_local_bytes;
    return b;
}

void mel_gpu_set_budget_pressure_callback(Mel_Gpu_Device* dev, Mel_Gpu_Budget_Pressure_Fn cb, void* user)
{
    if (!dev)
        return;
    dev->budget_pressure_cb = cb;
    dev->budget_pressure_user = user;
}
