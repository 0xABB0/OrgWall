#pragma once

// D3D12 backend (gpu-rhi.md §12 M2 co-primary; design/gpu-d3d12.md). COM is consumed through the C
// struct/vtable path: d3d12.h selects it automatically when __cplusplus is undefined, and COBJMACROS gives
// the ID3D12X_Method(obj, ...) convenience macros. IID_* GUID symbols come from dxguid.lib.
#define COBJMACROS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>

#include <core/types.h>
#include <allocator/allocator.h>
#include <reactor/reactor.h>
#include <thread/mutex.h>
#include <thread/thread.h>
#include <debug/assert.h>

#include <gpu/handle.h>
#include <gpu/caps.h>
#include <gpu/device.h>
#include <gpu/future.h>
#include <gpu/threading.h>
#include <gpu/queue.h>

// Phase 0 (device foundation): instance / adapter / device + direct queue + the backend-agnostic reactor
// pump (U3) and thread-safety tracker (U36). Resource tables, the deferred-free watermark (§3.3), the
// allocator (U8), and the submit→future fence poller land with their units in later phases.

struct Mel_Gpu_Instance
{
    IDXGIFactory6*       factory;
    Mel_Gpu_Debug_Config debug;
    const Mel_Alloc*     alloc;
    Mel_Gpu_Adapter*     adapters;
    u32                  adapter_count;
    bool                 debug_layer; // ID3D12Debug::EnableDebugLayer succeeded (validation analog)
};

struct Mel_Gpu_Adapter
{
    Mel_Gpu_Instance* instance;
    IDXGIAdapter1*    dxgi; // owned ref (released at instance_destroy)
    Mel_Gpu_Caps      caps;
};

struct Mel_Gpu_Device
{
    Mel_Gpu_Instance*        instance;
    Mel_Gpu_Adapter*         adapter;
    ID3D12Device*            d3d;
    ID3D12CommandQueue*      direct_queue; // D3D12 DIRECT queue == graphics/compute/copy (U7)
    Mel_Gpu_Caps             caps;
    const Mel_Alloc*         alloc;
    Mel_Reactor*             reactor;
    Mel_Gpu_Completion_Pump* pump;
    Mel_Gpu_Thread_Tracker*  tracker;
    Mel_Gpu_Debug_Config     debug;
    Mel_Gpu_Device_Lost_Fn   on_device_lost;
    void*                    device_lost_user;
    bool                     lost;
    bool                     owns_instance;
};

// caps.c — adapter-domain caps fillable from the DXGI descriptor alone (at instance-create, before a device
// exists), then device-level caps refined once an ID3D12Device + direct queue are available (mirrors the
// Vulkan refine-at-device-create split).
void mel_gpu__caps_from_adapter(IDXGIAdapter1* adapter, Mel_Gpu_Caps* out);
void mel_gpu__caps_refine_device(ID3D12Device* dev, ID3D12CommandQueue* queue, Mel_Gpu_Caps* out);
