#include "mtl_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

Mel_Gpu_Instance* mel_gpu_instance_create_opt(Mel_Gpu_Instance_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    NSArray<id<MTLDevice>>* devices = nil;
#if TARGET_OS_OSX
    devices = MTLCopyAllDevices();
#endif
    u32 count = (u32)devices.count;
    if (count == 0)
    {
        id<MTLDevice> sys = MTLCreateSystemDefaultDevice();
        if (sys)
        {
            devices = @[ sys ];
            count = 1;
        }
    }

    Mel_Gpu_Instance* inst = mel_alloc_type(alloc, Mel_Gpu_Instance);
    *inst = (Mel_Gpu_Instance){ 0 };
    inst->debug = opt.debug;
    inst->alloc = alloc;

    if (count == 0)
    {
        mel_log_error("gpu", "no Metal devices found");
        inst->adapters = NULL;
        inst->adapter_count = 0;
        return inst;
    }

    inst->adapters = mel_alloc_array(alloc, Mel_Gpu_Adapter, count);
    inst->adapter_count = count;
    for (u32 i = 0; i < count; i++)
    {
        id<MTLDevice> mtl = devices[i];
        inst->adapters[i].instance = inst;
        inst->adapters[i].mtl = mtl;
        mel_gpu__caps_probe(mtl, &inst->adapters[i].caps);
        inst->adapters[i].caps.debug.validation_available = opt.debug.enabled;
    }

    mel_log_info("gpu", "metal instance created: %u adapter(s)", count);
    return inst;
}

void mel_gpu_instance_destroy(Mel_Gpu_Instance* inst)
{
    if (!inst)
        return;
    if (inst->adapters)
    {
        for (u32 i = 0; i < inst->adapter_count; i++)
            inst->adapters[i].mtl = nil;
        mel_dealloc(inst->alloc, inst->adapters);
    }
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
