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
#include <collection.slotmap/slotmap.h>
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
#include <gpu/memory.h>
#include <gpu/buffer.h>

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

typedef struct
{
    Mel_SlotMap map;
    bool        init;
} Mel_Gpu_Resource_Table;

// U9 buffer: a committed resource owns its own implicit heap (the dedicated-allocation floor; U8 placed-
// resource suballocation lands later). UPLOAD/READBACK heaps keep a persistent map.
typedef struct
{
    Mel_Gpu_Resource_Header   header;
    ID3D12Resource*           resource;
    void*                     mapped;  // persistent map for host-visible (UPLOAD/READBACK) buffers
    u64                       size;
    bool                      host_visible;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_va; // for index/vertex views and the U14 pointer payload
} Mel_Gpu_Buffer_Obj;

// U3 future-gated retirement (gpu-rhi.md §3.3): a destroyed resource's COM objects are released only once
// every submission that could reference it has retired. No enum tag (MEL-CODE-001) — every non-null is freed.
typedef struct
{
    u64                     marker;
    ID3D12Resource*         resource;
    ID3D12PipelineState*    pso;
    ID3D12RootSignature*    root_sig;
    Mel_Gpu_Resource_Table* reclaim_table;
    u32                     reclaim_index;
    bool                    has_reclaim;
} Mel_Gpu_Deferred_Free;

// U7: a reactor-driven submit awaiting the device timeline fence to reach its serial.
typedef struct
{
    Mel_Gpu_Future* future;
    u64             serial;
} Mel_Gpu_Pending_Submit;

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

    // U7/U3: one device timeline fence drives both submit→future completion and the §3.3 deferred-free
    // watermark. The signal value IS the submission serial (single DIRECT queue ⇒ in-order completion).
    ID3D12Fence* timeline;
    HANDLE       fence_event;

    Mel_Mutex              submit_lock;
    u64                    submit_serial;    // last reserved serial == last signaled fence value
    u64                    submit_completed; // watermark: highest retired serial
    Mel_Gpu_Deferred_Free* deferred;
    u32                    deferred_count;
    u32                    deferred_cap;

    Mel_Gpu_Pending_Submit* pending; // reactor submits awaiting fence completion
    u32                     pending_count;
    u32                     pending_cap;
    bool                    submit_poller_registered;

    // U1 slotmap-per-type resource tables (grown per phase).
    Mel_Mutex              obj_lock;
    Mel_Gpu_Resource_Table buffers;

    // U8 residency: budget via IDXGIAdapter3::QueryVideoMemoryInfo; the callback fires on over-budget create.
    void (*budget_pressure_cb)(struct Mel_Gpu_Device*, Mel_Gpu_Memory_Budget, void*);
    void* budget_pressure_user;
};

struct Mel_Gpu_Queue
{
    Mel_Gpu_Device*    dev;
    ID3D12CommandQueue* d3d;
    Mel_Gpu_Queue_Role role;
    bool               internally_synchronized;
    bool               locked_fallback;
};

// caps.c — adapter-domain caps from the DXGI descriptor (instance-create), refined once a device exists.
void mel_gpu__caps_from_adapter(IDXGIAdapter1* adapter, Mel_Gpu_Caps* out);
void mel_gpu__caps_refine_device(ID3D12Device* dev, ID3D12CommandQueue* queue, Mel_Gpu_Caps* out);

// device.c — U1 slotmap table helpers (mutex-guarded; the slotmap is the per-type allocator).
Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj);
void*              mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove_deferred(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
void               mel_gpu__table_reclaim(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, u32 index);

// device.c — U3 future-gated retirement watermark.
u64  mel_gpu__submit_serial_next(Mel_Gpu_Device* dev);
void mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial);
void mel_gpu__defer_free(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free entry);
void mel_gpu__wait_serial(Mel_Gpu_Device* dev, u64 serial); // block until the timeline fence reaches serial
