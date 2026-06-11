#include "d3d_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

static bool mel_gpu__enable_debug_layer(void)
{
    ID3D12Debug* dbg = NULL;
    if (FAILED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&dbg)) || !dbg)
        return false;
    ID3D12Debug_EnableDebugLayer(dbg);
    ID3D12Debug_Release(dbg);
    return true;
}

Mel_Gpu_Instance* mel_gpu_instance_create_opt(Mel_Gpu_Instance_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    bool want_debug = opt.debug.enabled;
    bool debug_layer = want_debug && mel_gpu__enable_debug_layer();
    if (want_debug && !debug_layer)
        mel_log_warn("gpu", "D3D12 debug layer requested but unavailable (install the Graphics Tools feature)");

    UINT           flags = debug_layer ? DXGI_CREATE_FACTORY_DEBUG : 0u;
    IDXGIFactory6* factory = NULL;
    HRESULT        hr = CreateDXGIFactory2(flags, &IID_IDXGIFactory6, (void**)&factory);
    if (FAILED(hr) && flags)
    {
        mel_log_warn("gpu", "CreateDXGIFactory2(debug) failed: 0x%08lx; retrying without the debug flag", (unsigned long)hr);
        hr = CreateDXGIFactory2(0, &IID_IDXGIFactory6, (void**)&factory);
    }
    if (FAILED(hr) || !factory)
    {
        mel_log_error("gpu", "CreateDXGIFactory2 failed: 0x%08lx", (unsigned long)hr);
        return NULL;
    }

    Mel_Gpu_Instance* inst = mel_alloc_type(alloc, Mel_Gpu_Instance);
    *inst = (Mel_Gpu_Instance){ 0 };
    inst->factory = factory;
    inst->debug = opt.debug;
    inst->alloc = alloc;
    inst->debug_layer = debug_layer;

    IDXGIAdapter1* found[16];
    u32            n = 0;
    for (UINT i = 0; n < 16; i++)
    {
        IDXGIAdapter1* a = NULL;
        if (IDXGIFactory6_EnumAdapterByGpuPreference(factory, i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, (void**)&a) == DXGI_ERROR_NOT_FOUND)
            break;
        if (!a)
            break;
        found[n++] = a;
    }

    if (n == 0)
    {
        mel_log_error("gpu", "no DXGI adapters found");
        inst->adapters = NULL;
        inst->adapter_count = 0;
        return inst;
    }

    inst->adapters = mel_alloc_array(alloc, Mel_Gpu_Adapter, n);
    inst->adapter_count = n;
    for (u32 i = 0; i < n; i++)
    {
        inst->adapters[i].instance = inst;
        inst->adapters[i].dxgi = found[i];
        mel_gpu__caps_from_adapter(found[i], &inst->adapters[i].caps);
        inst->adapters[i].caps.debug.validation_available = debug_layer;
    }

    mel_log_info("gpu", "d3d12 instance created: %u adapter(s)%s", n, debug_layer ? ", debug layer on" : "");
    return inst;
}

void mel_gpu_instance_destroy(Mel_Gpu_Instance* inst)
{
    if (!inst)
        return;
    if (inst->adapters)
    {
        for (u32 i = 0; i < inst->adapter_count; i++)
            if (inst->adapters[i].dxgi)
                IDXGIAdapter1_Release(inst->adapters[i].dxgi);
        mel_dealloc(inst->alloc, inst->adapters);
    }
    if (inst->factory)
        IDXGIFactory6_Release(inst->factory);
    mel_dealloc(inst->alloc, inst);
}

u32 mel_gpu_adapters(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter** out, u32 max)
{
    if (!inst)
        return 0;
    u32 n = inst->adapter_count < max ? inst->adapter_count : max;
    for (u32 i = 0; i < n; i++)
        out[i] = &inst->adapters[i];
    return inst->adapter_count;
}

Mel_Gpu_Caps mel_gpu_adapter_caps(Mel_Gpu_Adapter* adapter) { return adapter ? adapter->caps : (Mel_Gpu_Caps){ 0 }; }
